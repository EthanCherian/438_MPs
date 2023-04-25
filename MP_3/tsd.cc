#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/empty.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include "helper.h"
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <glog/logging.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <set>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "sns.grpc.pb.h"
#include "snsCoordinator.grpc.pb.h"
#include "snsFollowSync.grpc.pb.h"

using csce438::allUsers;
using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::SNSService;
using csce438::User;
using csce438::UserStatus;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;
using snsCoordinator::ClusterId;
using snsCoordinator::Heartbeat;
using snsCoordinator::ServerType;
using snsCoordinator::SNSCoordinator;
using snsFollowSync::SNSFollowSync;

using std::begin;
using std::cout;
using std::distance;
using std::end;
using std::endl;
using std::find;
using std::find_if;
using std::fstream;
using std::ios;
using std::istringstream;
using std::mutex;
using std::ofstream;
using std::pair;
using std::set;
using std::shared_ptr;
using std::stoi;
using std::string;
using std::thread;
using std::to_string;
using std::unique_lock;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

string server_ip = "localhost";
string port = "10000"; // server's port
unique_ptr<SNSCoordinator::Stub> coordinator_stub;
mutex mu_;
ServerType opt_type;

// for master to use
unique_ptr<SNSService::Stub> slave_stub;
std::shared_ptr<ClientReaderWriter<Message, Message>> slave_stream;

void RewriteTimeline(string filename, ServerReaderWriter<Message, Message>* stream) {
	file_lock(filename);

	Message new_msg;
	string line;
	vector<string> all_messages;
	string filePath = truePath(filename + ".txt");
	std::ifstream in(filePath);

	// Read the entire timeline file into vector
	while (getline(in, line)) {
		if (in.eof() || in.bad()) break;
		if (line == "") continue;
		all_messages.push_back(line);
	}

	// Sort the vector of strings (time ascending)
	sort(all_messages.begin(), all_messages.end());

	log(INFO, "Total messages in " + filePath + " is: " << all_messages.size());

	file_unlock(filename);

	if (type == ServerType::MASTER) {
		// Send the newest 20 messages to the client to be displayed
		for (auto it = all_messages.rbegin(); it != all_messages.rend(); it++) {
			if (it - all_messages.rbegin() == 20) break;
			new_msg.set_msg(*it);
			stream->Write(new_msg);
		}
	}
}

void doFollow(string username1, string username2, const Request* request, Reply* reply) {

	if (following[stoi(username1)].count(stoi(username2)) == 1) {
		reply->set_msg("Join Failed -- Already Following User");
		return;
	}

	// Write to following file
	string currTime = to_string(time(NULL));

	file_lock(username1 + "_following");
	std::ofstream followingFile(truePath(username1 + "_following.txt"), std::ios::app | std::ios::out | std::ios::in);
	string content = username2 + " " + currTime + " \n";
	followingFile << content;
	file_unlock(username1 + "_following");

	reply->set_msg("Join Successful");

	// Forward to slave
	if (type == ServerType::MASTER && slave_stub != nullptr) {
		ClientContext s_context;
		Reply s_reply;
		Status status = slave_stub->Follow(&s_context, *request, &s_reply);

		if (status.ok()) {
			log(INFO, "Successfully forwarded login to slave");
		} else {
			log(ERROR, "Failed to forward login to slave");
		}
	}
}

class SNSServiceImpl final : public SNSService::Service {

	Status Check(ServerContext* context, const Request* request, Reply* reply) override {
		log(INFO, "Connection in cluster " << server_id << " successful!");

		return Status::OK;
	}

	Status MakeMaster(ServerContext* context, const Heartbeat* heartbeat, google::protobuf::Empty* empty) override {
		log(INFO, "This server became master");

		// this server should become master
		type = ServerType::MASTER;
		opt_type = ServerType::MASTER;

		// assign master directory
		directory = snsCoordinator::ServerType_Name(type) + "_" + server_id + "/";

		return Status::OK;
	}

	// get new slave_stub for master
	Status SendSlave(ServerContext* context, const Heartbeat* hb, Reply* reply) override {
		log(INFO, "Received new slave's connection info");

		if (type == ServerType::MASTER) {
			string slave_info = hb->server_ip() + ":" + hb->server_port();
			slave_stub = unique_ptr<SNSService::Stub>(SNSService::NewStub(
				grpc::CreateChannel(
					slave_info, grpc::InsecureChannelCredentials())));

			// ensure slave_stub is connected
			ClientContext s_context;
			Request s_req;
			Reply s_reply;
			Status s_status = slave_stub->Check(&s_context, s_req, &s_reply);

			if (s_status.ok()) {
				log(INFO, "Successfully connected to slave at " << slave_info);
			} else {
				log(ERROR, "Failed to connect to slave at " << slave_info);
			}
		}

		return Status::OK;
	}

	Status List(ServerContext* context, const Request* request, ListReply* list_reply) override {
		log(INFO, "Serving List Request");

		unique_lock<mutex> lock(mu_);
		for (auto uid : all_uids) {
			list_reply->add_all_users(to_string(uid));
		}

		for (auto [fid, ftme] : followers[stoi(request->user_id())]) {
			list_reply->add_followers(to_string(fid));
		}

		lock.unlock();

		return Status::OK;
	}

	Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
		log(INFO, "Serving Follow Request");

		string username1 = request->user_id();
		string username2 = request->arguments(0);
		if (all_uids.count(stoi(username2)) == 0)
			reply->set_msg("Join Failed -- Invalid Username");
		else {
			doFollow(username1, username2, request, reply);
		}
		return Status::OK;
	}

	Status Login(ServerContext* context, const Request* request, Reply* reply) override {
		// ------------------------------------------------------------
		// In this function, you are to write code that handles
		// a new user and verify if the username is available
		// or already taken
		// ------------------------------------------------------------
		string user_id = request->user_id();

		log(INFO, "Serving Login Request for user " << user_id);

		string allfilePath = "all_users.txt";

		file_lock("all_users");
		// Read the all_users.txt to see "updated" all users
		set<string> tempAllUids;
		string uid;
		std::ifstream allfileRead(truePath(allfilePath));
		while (getline(allfileRead, uid))
			tempAllUids.insert(uid);

		// Write to the all_users.txt if user not in db
		if (tempAllUids.count(user_id) == 0) {
			std::ofstream allfile(truePath(allfilePath), std::ios::app | std::ios::out | std::ios::in);
			allfile << (user_id + "\n");

			reply->set_msg("Login Successful!");

			// User follows self
			Request req;
			req.set_user_id(user_id);
			req.add_arguments(user_id);
			Reply res;
			doFollow(user_id, user_id, &req, &res);

			log(INFO, "User " + user_id + " written to all_users");
		}
		file_unlock("all_users");

		// Forward to slave
		if (type == ServerType::MASTER && slave_stub != nullptr) {
			log(INFO, "Forwarding user " + user_id + " to slave...");

			ClientContext s_context;
			Reply s_reply;
			Status s_status = slave_stub->Login(&s_context, *request, &s_reply);

			if (s_status.ok()) {
				log(INFO, "Successfully forwarded login to slave");
			} else {
				log(ERROR, "Failed to forward login to slave");
			}
		}

		return Status::OK;
	}

	Status Timeline(ServerContext* context,
					ServerReaderWriter<Message, Message>* stream) override {

		Message first;
		stream->Read(&first);
		string clientname = first.user_id();

		log(INFO, "Serving Timeline Request for user " << clientname);

		string filename = clientname + "_timeline";
		bool flag = 1; // determines whether writer should continue or not

		ClientContext s_context;
		if (type == ServerType::MASTER && slave_stub != nullptr) {
			slave_stream = slave_stub->Timeline(&s_context);

			// also this log checking first.user_id() somehow fixed filename in slave dir
			log(INFO, "Pushing first message from " << first.user_id() << " to slave");

			slave_stream->Write(first);
		}

		thread reader([clientname, filename, &flag, stream, &slave_stream]() mutable {
			Message message;
			while (stream->Read(&message)) {
				string username = message.user_id();

				// Write the current message to user's "timeline.txt"
				ofstream user_file(truePath(filename + ".txt"), ios::app | ios::out | ios::in);
				google::protobuf::Timestamp temptime = message.timestamp();
				string time = google::protobuf::util::TimeUtil::ToString(temptime);
				string fileinput = time + " :: " + message.user_id() + ":" + message.msg() + "\n";

				file_lock(filename);
				user_file << fileinput;
				file_unlock(filename);

				// push message to slave
				if (type == ServerType::MASTER && slave_stream != nullptr) {
					slave_stream->Write(message);
				}
			}
			flag = 0;
			
			if (type == ServerType::MASTER && slave_stream != nullptr) {
				slave_stream->WritesDone();
			}
		});

		thread writer([clientname, filename, flag, stream]() {
			RewriteTimeline(filename, stream); // Initial write
			fileChanged(truePath(filename + ".txt")); // Initial check
			
			while (flag) {
				// File changed within recently?
				if (fileChanged(truePath(filename + ".txt"))) {
					RewriteTimeline(filename, stream);
				}

				// Monitor for changes every 10 seconds
				std::this_thread::sleep_for(std::chrono::seconds(10));
			}
		});

		// Wait for the threads to finish
		writer.join();
		reader.join();

		return Status::OK;
	}
};

void fs_monitor() {

	// runs every 2 seconds
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(10));

		vector<pair<string, string>> filePaths;
		getChangedFiles(filePaths, &isNotTimeline, false);

		for (auto& [filepath, filename] : filePaths) {
			file_lock(filename);
			std::unique_lock<std::mutex> lock(mu_);
			std::ifstream infile(filepath);
			string uid, fileType;
			int other_id, uidInt;
			long long time;

			istringstream ss(filename);
			getline(ss, uid, '_');
			getline(ss, fileType, '_');

			if (fileType == "followers") {
				uidInt = stoi(uid);
				while (infile >> other_id >> time) {
					if (infile.eof() && infile.bad()) break;
					followers[uidInt][other_id] = time;

					log(INFO, "Added following " << other_id << " to user " << uidInt);
				}
			} else if (fileType == "following") {
				uidInt = stoi(uid);
				while (infile >> other_id >> time) {
					if (infile.eof() && infile.bad()) break;
					following[uidInt][other_id] = time;

					log(INFO, "Added follower " << other_id << " to user " << uidInt);
				}
			} else if (fileType == "users") { // part of all_users
				while (infile >> uidInt) {
					if (infile.eof() && infile.bad()) break;
					all_uids.insert(uidInt);

					log(INFO, "Added user " << uidInt << " to server's db");
				}
			}
			file_unlock(filename);
		}
	}
}

void sendHeartBeats(string cip, string cp) {
	// setup heartbeat and stream
	ClientContext context;
	shared_ptr<ClientReaderWriter<Heartbeat, Heartbeat>> stream(
		coordinator_stub->HandleHeartBeats(&context));

	Heartbeat hb;

	bool first = true;

	// send/read heartbeats
	while (true) {
		hb.set_server_id(stoi(server_id));
		hb.set_server_type(opt_type);
		hb.set_server_ip(server_ip);
		hb.set_server_port(port);
		hb.set_allocated_timestamp(protoGetNow());

		log(INFO, "Sending heartbeat at " << hb.timestamp().seconds() << "s"
										  << " for type " << snsCoordinator::ServerType_Name(opt_type));

		// send heartbeat
		stream->Write(hb);

		// read heartbeat if any and update server type when read
		if (first) {
			Heartbeat reply_hb;
			if (stream->Read(&reply_hb)) {
				log(INFO, "Reading reply for " << opt_type << " return type " << snsCoordinator::ServerType_Name(reply_hb.server_type()));
				if (opt_type == ServerType::MASTER) {
					if (reply_hb.server_type() == ServerType::SLAVE) {
						// A master already exists, become a slave
						type = ServerType::SLAVE;
						opt_type = type;
						hb.set_server_type(ServerType::SLAVE);

						// assign slave directory
						directory = snsCoordinator::ServerType_Name(type) + "_" + server_id + "/";

						std::filesystem::path m_dir = snsCoordinator::ServerType_Name(ServerType::MASTER) + "_" + server_id + "/";

						// copy master directory to slave directory
						std::filesystem::copy(m_dir, directory, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
					}
				}
				type = opt_type;
			}
			first = false;
		}

		std::this_thread::sleep_for(std::chrono::seconds(10));
	}
}

void RunServer(string cip, string cp) {
	// ------------------------------------------------------------
	// In this function, you are to write code
	// which would start the server, make it listen on a particular
	// port number.
	// ------------------------------------------------------------

	log(INFO, "Connecting to coordinator... " + cip + ":" + cp);

	// Connect to coordinator
	string coordinator_info = cip + ":" + cp;
	coordinator_stub = unique_ptr<SNSCoordinator::Stub>(SNSCoordinator::NewStub(
		grpc::CreateChannel(
			coordinator_info, grpc::InsecureChannelCredentials())));

	log(INFO, "Connected to coordinator " + cip + ":" + cp);

	// send heartbeats to coordinator
	thread heartBeatThread(sendHeartBeats, cip, cp);
	heartBeatThread.detach();

	// make sure this server's type is set from first heartbeat
	while (type == ServerType::NONE) {
		log(INFO, "Waiting for server type to be set... ");

		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	// assign file directory
	directory = snsCoordinator::ServerType_Name(type) + "_" + server_id;
	mkdir(("./" + directory.string()).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	directory += "/";

	initialize_write_times();
	initialize_localdb(mu_);

	// connect to slave if this is a master
	if (type == ServerType::MASTER) {
		log(INFO, "Connecting to slave... ");

		ClientContext context;
		ClusterId clusterId;
		snsCoordinator::Server slave;

		// get slave from coordinator
		clusterId.set_cluster(stoi(server_id));
		coordinator_stub->GetSlave(&context, clusterId, &slave);

		// make slave stub
		string slaveInfo = slave.server_ip() + ":" + slave.port_num();
		slave_stub = std::unique_ptr<SNSService::Stub>(SNSService::NewStub(
			grpc::CreateChannel(
				slaveInfo, grpc::InsecureChannelCredentials())));

		ClientContext s_context;
		Request s_req;
		Reply s_reply;
		Status s_status = slave_stub->Check(&s_context, s_req, &s_reply);

		while (!s_status.ok()) {
			log(INFO, "Waiting for slave to start");

			std::this_thread::sleep_for(std::chrono::seconds(1));

			ClientContext s_context;
			Request s_req;
			Reply s_reply;
			s_status = slave_stub->Check(&s_context, s_req, &s_reply);
		}

		log(INFO, "Connected to slave " << slaveInfo << " with status: " << s_status.ok());
	}

	// Thread to monitor local file system, excluding the timeline files, and update local db immediately
	thread fsThread(fs_monitor);
	fsThread.detach();

	// connect SNSService and start server
	SNSServiceImpl service;

	ServerBuilder builder;
	string server_address(server_ip + ":" + port);
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	unique_ptr<Server> server(builder.BuildAndStart());

	cout << "Server listening on " << server_address << endl;
	log(INFO, "Server listening on " + server_address);

	server->Wait();
}

static struct option long_options[] = {
	{"cip", required_argument, 0, 'c'},
	{"cp", required_argument, 0, 'i'},
	{"p", required_argument, 0, 'p'},
	{"id", required_argument, 0, 'd'},
	{"t", required_argument, 0, 't'},
	{0, 0, 0, 0}};

int main(int argc, char** argv) {
	string cip = "localhost"; // coordinator host
	string cp = "9090";		  // coordinator port

	int option_index = 0;
	int opt = 0;
	while ((opt = getopt_long(argc, argv, "c:i:p:d:t:", long_options, &option_index)) != -1) {
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
				server_id = optarg;
				break;
			case 't':
				if (string(optarg) == "master") {
					opt_type = ServerType::MASTER;
				} else if (string(optarg) == "slave") {
					opt_type = ServerType::SLAVE;
				} else {
					std::cerr << "Invalid Server Type\n";
					return 1;
				}
				break;
			default:
				std::cerr << "Invalid Command Line Argument\n";
				break;
		}
	}

	string log_file_name = snsCoordinator::ServerType_Name(opt_type) + server_id;
	google::InitGoogleLogging(log_file_name.c_str());
	log(INFO, "Logging Initialized. Server starting... with coord: " + cip + ":" + cp + " p: " + port + " id: " + server_id + " t: " + snsCoordinator::ServerType_Name(opt_type));

	RunServer(cip, cp);

	return 0;
}
