#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <deque>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
using csce438::SNSService;
using csce438::Reply;
using csce438::Request;
using grpc::Status;
using grpc::ClientContext;

using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::map;
using std::set;
using std::deque;
using std::stringstream;

class Client : public IClient
{
    public:
        Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
            :hostname(hname), username(uname), port(p)
            {}
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();
    private:
        std::string hostname;
        std::string username;
        std::string port;
        
        // You can have an instance of the client stub
        // as a member variable.
        std::unique_ptr<SNSService::Stub> stub_;
};

int main(int argc, char** argv) {

    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
        switch(opt) {
            case 'h':
                hostname = optarg;break;
            case 'u':
                username = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);
    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

int Client::connectTo()
{
	// ------------------------------------------------------------
    // In this function, you are supposed to create a stub so that
    // you call service methods in the processCommand/porcessTimeline
    // functions. That is, the stub should be accessible when you want
    // to call any service methods in those functions.
    // I recommend you to have the stub as
    // a member variable in your own Client class.
    // Please refer to gRpc tutorial how to create a stub.
	// ------------------------------------------------------------
	auto channel = grpc::CreateChannel(hostname+":"+port, grpc::InsecureChannelCredentials());
	stub_ = SNSService::NewStub(channel);
	
	ClientContext cliCon;
	Request req;
	Reply rep;
	req.set_username(username);
	Status loginStat = stub_->Login(&cliCon, req, &rep);

    return (loginStat.ok()) ? 1 : -1; // return 1 if success, otherwise return -1
}

IReply Client::processCommand(std::string& input)
{
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
    // Suppose you have "Follow" service method for FOLLOW command,
    // IReply can be set as follow:
    // 
    //     // some codes for creating/initializing parameters for
    //     // service method
    //     IReply ire;
    //     grpc::Status status = stub_->Follow(&context, /* some parameters */);
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
    stringstream ss(input);

    string command = "";
    ss >> command;
    
	string user = "";
	ss >> user;
	
    ClientContext cliCon;
    Request req;
    Reply rep;
    
	Status stat;
    IReply ire;
	req.set_username(username);		// request coming from user
	if (command == "FOLLOW") {
		req.add_arguments(user);	// user to be followed
		stat = stub_->Follow(&cliCon, req, &rep);
		cout << rep.msg() << endl;
		ire.grpc_status = stat;
		string m = rep.msg();
		if (m == "doesn't exist") {
			ire.comm_status = FAILURE_INVALID_USERNAME;
		} else if (m == "already following") {	
			ire.comm_status = FAILURE_ALREADY_EXISTS;
		} else {
			ire.comm_status = (stat.ok()) ? SUCCESS : FAILURE_INVALID;
		}
		if (stat.ok())			// update following users if successful
			ire.following_users = { user };
	} else if (command == "UNFOLLOW") {
		cout << " in unfollow" << endl;
		stat = stub_->UnFollow(&cliCon, req, &rep);
	} else if (command == "LIST") {
		stat = stub_->List(&cliCon, req, &rep);
		ire.all_users = {rep.all_users().begin(), rep.all_users().end()};
		ire.following_users = {rep.following_users().begin(), rep.following_users().end()};
		ire.grpc_status = stat;
		ire.comm_status = (stat.ok()) ? SUCCESS : FAILURE_INVALID;
	} else if (command == "TIMELINE") {
		cout << " in timeline" << endl;
		//stat = stub_->Timeline(&cliCon, &req, &rep);
	}
    
    return ire;
}

void Client::processTimeline()
{
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
