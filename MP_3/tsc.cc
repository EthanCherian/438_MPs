#include "client.h"
#include <getopt.h>
#include <glog/logging.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "sns.grpc.pb.h"
#include "snsCoordinator.grpc.pb.h"

using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::SNSService;
using google::protobuf::Timestamp;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using grpc::StatusCode;
using snsCoordinator::Server;
using snsCoordinator::SNSCoordinator;
using snsCoordinator::User;

using std::begin;
using std::cout;
using std::distance;
using std::end;
using std::endl;
using std::find;
using std::find_if;
using std::fstream;
using std::ios;
using std::mutex;
using std::pair;
using std::set;
using std::shared_ptr;
using std::stoi;
using std::string;
using std::thread;
using std::unique_lock;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

Message MakeMessage(const std::string& user_id, const std::string& msg) {
	Message m;
	m.set_user_id(user_id);
	m.set_msg(msg);
	google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
	timestamp->set_seconds(time(NULL));
	timestamp->set_nanos(0);
	m.set_allocated_timestamp(timestamp);
	return m;
}

class Client : public IClient {
public:
	Client(const string& cip,
		   const string& cp,
		   const string& user_id,
		   const string& port)
		: cip(cip), cp(cp), user_id(user_id), port(port) {
	}

protected:
	virtual IReply connectTo();
	virtual IReply processCommand(string& input);
	virtual void processTimeline();

private:
	string cip;
	string cp;
	string user_id;
	string port;

	// You can have an instance of the client stub
	// as a member variable.
	unique_ptr<SNSService::Stub> server_stub;
	unique_ptr<SNSCoordinator::Stub> coordinator_stub;

	IReply Login();
	IReply List();
	IReply Follow(const string& user_id2);
	void Timeline(const string& user_id);
};

static struct option long_options[] = {
	{"cip", required_argument, 0, 'c'},
	{"cp", required_argument, 0, 'i'},
	{"p", required_argument, 0, 'p'},
	{"id", required_argument, 0, 'd'},
	{0, 0, 0, 0}};

int main(int argc, char** argv) {
	string user_id = "1";
	string port = "10000";	  // clients's port
	string cip = "localhost"; // coordinator host
	string cp = "9090";		  // coordinator port

	int option_index = 0;
	int opt = 0;
	while ((opt = getopt_long(argc, argv, "c:i:p:d:", long_options, &option_index)) != -1) {
		switch (opt) {
			case 'c':
				cip = optarg;
				break;
			case 'i':
				cp = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			case 'd':
				user_id = optarg;
				break;
			default:
				std::cerr << "Invalid Command Line Argument\n";
				break;
		}
	}

	string log_file_name = string("client-") + user_id;
	google::InitGoogleLogging(log_file_name.c_str());
	log(INFO, "Logging Initialized. Client starting...");

	Client myc(cip, cp, user_id, port);

	myc.run_client();

	return 0;
}

IReply Client::Login() {
	// Get server info from coordinator
	ClientContext context;
	User user;
	user.set_user_id(stoi(user_id));
	Server server;
	Status status = coordinator_stub->GetServer(&context, user, &server);
	if (!status.ok()) {
		// cordinator failed
		log(ERROR, "Connection to server failed: " << cip << ":" << cp);
		exit(1);
	}

	// Connect to server
	string login_info = server.server_ip() + ":" + server.port_num();

	server_stub = unique_ptr<SNSService::Stub>(SNSService::NewStub(
		grpc::CreateChannel(
			login_info, grpc::InsecureChannelCredentials())));

	// try login
	ClientContext s_context;
	Request request;
	request.set_user_id(user_id);
	Reply reply;
	status = server_stub->Login(&s_context, request, &reply);

	IReply ire;
	ire.server_info = login_info;
	ire.grpc_status = status;
	if (!ire.grpc_status.ok()) {
		log(INFO, "Trying to connect to server...");

		std::this_thread::sleep_for(std::chrono::seconds(4));

		ire = Login();
	}

	return ire;
}

IReply Client::connectTo() {
	// ------------------------------------------------------------
	// In this function, you are supposed to create a stub so that
	// you call service methods in the processCommand/porcessTimeline
	// functions. That is, the stub should be accessible when you want
	// to call any service methods in those functions.
	// I recommend you to have the stub as
	// a member variable in your own Client class.
	// Please refer to gRpc tutorial how to create a stub.
	// ------------------------------------------------------------

	// Connect to coordinator
	string coordinator_info = cip + ":" + cp;
	coordinator_stub = unique_ptr<SNSCoordinator::Stub>(SNSCoordinator::NewStub(
		grpc::CreateChannel(
			coordinator_info, grpc::InsecureChannelCredentials())));

	return Login();
}

IReply Client::processCommand(std::string& input) {
	// ------------------------------------------------------------
	// GUIDE 1:
	// In this function, you are supposed to parse the given input
	// command and create your own message so that you call an
	// appropriate service method. The input command will be one
	// of the followings:
	//
	// FOLLOW <username>
	// UNFOLLOW <username>
	// LIST
	// TIMELINE
	//
	// - JOIN/LEAVE and "<username>" are separated by one space.
	// ------------------------------------------------------------

	// ------------------------------------------------------------
	// GUIDE 2:
	// Then, you should create a variable of IReply structure
	// provided by the client.h and initialize it according to
	// the result. Finally you can finish this function by returning
	// the IReply.
	// ------------------------------------------------------------

	// ------------------------------------------------------------
	// HINT: How to set the IReply?
	// Suppose you have "Join" service method for JOIN command,
	// IReply can be set as follow:
	//
	//     // some codes for creating/initializing parameters for
	//     // service method
	//     IReply ire;
	//     grpc::Status status = stub_->Join(&context, /* some parameters */);
	//     ire.grpc_status = status;
	//     if (status.ok()) {
	//         ire.comm_status = SUCCESS;
	//     } else {
	//         ire.comm_status = FAILURE_NOT_EXISTS;
	//     }
	//
	//      return ire;
	//
	// IMPORTANT:
	// For the command "LIST", you should set both "all_users" and
	// "following_users" member variable of IReply.
	// ------------------------------------------------------------
	IReply ire;
	std::size_t index = input.find_first_of(" ");

	log(INFO, "Processing " + input);

	if (index != std::string::npos) {
		std::string cmd = input.substr(0, index);

		std::string argument = input.substr(index + 1, (input.length() - index));

		if (cmd == "FOLLOW") {
			return Follow(argument);
		}
	} else {
	if (input == "LIST") {
			return List();
		} else if (input == "TIMELINE") {
			ire.comm_status = SUCCESS;
			return ire;
		}
	}

	ire.comm_status = FAILURE_INVALID;
	return ire;
}

IReply Client::List() {
	// Data being sent to the server
	Request request;
	request.set_user_id(user_id);

	// Container for the data from the server
	ListReply list_reply;

	// Context for the client
	ClientContext context;

	Status status = server_stub->List(&context, request, &list_reply);

	log(INFO, "Called server for LIST");

	IReply ire;
	ire.grpc_status = status;
	// Loop through list_reply.all_users and list_reply.following_users
	// Print out the name of each room
	if (status.ok()) {
		ire.comm_status = SUCCESS;
		std::string all_users;
		std::string following_users;
		for (std::string s : list_reply.all_users()) {
			ire.all_users.push_back(s);
		}
		for (std::string s : list_reply.followers()) {
			ire.followers.push_back(s);
		}

		log(INFO, "LIST successful");
	} else {
		displayReConnectionMessage(Login().server_info);
		ire = List();
	}

	return ire;
}

IReply Client::Follow(const std::string& user_id2) {
	Request request;
	request.set_user_id(user_id);
	request.add_arguments(user_id2);

	log(INFO, "Attempting to follow user " << user_id2 << " by user " << user_id);

	Reply reply;
	ClientContext context;

	Status status = server_stub->Follow(&context, request, &reply);
	IReply ire;
	ire.grpc_status = status;
	if (reply.msg() == "Join Failed -- Invalid Username") {
		ire.comm_status = FAILURE_INVALID_USERID;
	} else if (reply.msg() == "Join Failed -- Already Following User") {
		ire.comm_status = FAILURE_ALREADY_EXISTS;
	} else if (reply.msg() == "Join Successful") {
		ire.comm_status = SUCCESS;

		log(INFO, "User " << user_id << " successfully followed " << user_id2);
	} else {
		displayReConnectionMessage(Login().server_info);
		Follow(user_id2);
	}
	return ire;
}

void Client::processTimeline() {
	Timeline(user_id);
	// ------------------------------------------------------------
	// In this function, you are supposed to get into timeline mode.
	// You may need to call a service method to communicate with
	// the server. Use getPostMessage/displayPostMessage functions
	// for both getting and displaying messages in timeline mode.
	// You should use them as you did in hw1.
	// ------------------------------------------------------------

	// ------------------------------------------------------------
	// IMPORTANT NOTICE:
	//
	// Once a user enter to timeline mode , there is no way
	// to command mode. You don't have to worry about this situation,
	// and you can terminate the client program by pressing
	// CTRL-C (SIGINT)
	// ------------------------------------------------------------
}

void Client::Timeline(const std::string& username) {
	ClientContext context;

	std::shared_ptr<ClientReaderWriter<Message, Message>> stream(
		server_stub->Timeline(&context));

	// Thread used to read chat messages and send them to server
	std::thread writer([username, stream, this]() {
		std::string input = "Set Stream";
		Message m = MakeMessage(username, input);
		bool write_success = stream->Write(m);
		if (!write_success) {
			log(ERROR, "Failed to write message to server, attempting to reconnect");

			displayReConnectionMessage(Login().server_info);
			Timeline(username);
		}
		while (1) {
			input = getPostMessage();
			m = MakeMessage(username, input);
			write_success = stream->Write(m);
			if (!write_success) {
				displayReConnectionMessage(Login().server_info);
				Timeline(username);
			}
		}
		stream->WritesDone();
	});

	std::thread reader([username, stream, this]() {
		Message m;
		while (true) {
			bool read_success = stream->Read(&m);
			if (!read_success) {
				displayReConnectionMessage(Login().server_info);
				Timeline(username);
			}

			google::protobuf::Timestamp temptime = m.timestamp();
			std::time_t time = temptime.seconds();
			displayPostMessage(m.user_id(), m.msg(), time);
		}
	});

	// Wait for the threads to finish
	writer.join();
	reader.join();
}
