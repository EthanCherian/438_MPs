#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <set>
#include <mutex>
#include <map>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

using google::protobuf::Timestamp;
using google::protobuf::Duration;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using csce438::Message;
using csce438::Request;
using csce438::Reply;
using csce438::SNSService;

using std::cout;
using std::endl;
using std::set;
using std::string;
using std::deque;
using std::map;

struct User {
	string username;
	set<string> followers;
	set<string> following;
	deque<string> timeline; 	// max size 20
	
	User(string u) {
		username = u;
		followers.insert(u);		// new users start following themselves
		following.insert(u);
	}
};

class SNSServiceImpl final : public SNSService::Service {
  
  Status List(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // LIST request from the user. Ensure that both the fields
    // all_users & following_users are populated
    // ------------------------------------------------------------
    string user = request->username();
	User* u = existing_users[user];
	
    for (auto it = existing_users.begin(); it != existing_users.end(); it++) {
    	reply->add_all_users(it->first);
    }
    for (auto follower_name : u->following) {
    	reply->add_following_users(follower_name);
    }
    return Status::OK;
  }

  Status Follow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to follow one of the existing
    // users
    // ------------------------------------------------------------
    string user = request->username();
	string followee = *(request->arguments().begin());	// user to follow
    cout << "received follow request from " << user << " to " << followee << endl;
    std::unique_lock<std::mutex> lock(mut);
    
    User* u = existing_users[user];
    if (existing_users.count(followee) == 0) {		// followee doesn't exist
    	reply->set_msg("doesn't exist");
    	return Status::OK;			// can't return different status >:(
    } else if (u->following.count(followee) != 0) {	// already following
    	reply->set_msg("already following");
    	return Status::OK;
    }
    
	User* follUser = existing_users[followee];
	follUser->followers.insert(user);
	u->following.insert(followee);
	reply->set_msg("all good");
    return Status::OK; 
  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to unfollow one of his/her existing
    // followers
    // ------------------------------------------------------------
    return Status::OK;
  }
  
  Status Login(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // a new user and verify if the username is available
    // or already taken
    // ------------------------------------------------------------
    string user = request->username();
    std::unique_lock<std::mutex> lck(mut);
    if (all_users.count(user) != 0)			// user currently active
    	return Status::CANCELLED;
    
    all_users.insert(user);
    if(existing_users.count(user) == 0) {	// new user
    	User u = User(user);
    	existing_users.insert(std::make_pair(user, new User(user)));
    }
    
    return Status::OK;
  }

  Status Timeline(ServerContext* context, ServerReaderWriter<Message, Message>* stream) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // receiving a message/post from a user, recording it in a file
    // and then making it available on his/her follower's streams
    // ------------------------------------------------------------
    return Status::OK;
  }
  
  set<string> all_users;
  set<string> following_users;
  map<string, User*> existing_users;
  std::mutex mut;
};

void RunServer(std::string port_no) {
  // ------------------------------------------------------------
  // In this function, you are to write code 
  // which would start the server, make it listen on a particular
  // port number.
  // ------------------------------------------------------------
	string server_address = "localhost:"+port_no;
	SNSServiceImpl service;
	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	std::unique_ptr<Server> server(builder.BuildAndStart());
	
	cout << "Server listening on port " << server_address << endl;
	server->Wait();
}

int main(int argc, char** argv) {
  std::string port = "3010";
  int opt = 0;
  while ((opt = getopt(argc, argv, "p:")) != -1){
    switch(opt) {
      case 'p':
          port = optarg;
          break;
      default:
	      std::cerr << "Invalid Command Line Argument\n";
    }
  }
  RunServer(port);
  return 0;
}
