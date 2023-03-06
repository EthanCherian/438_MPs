#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <fstream>
#include <signal.h>
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
	deque<Message> timeline; 	// max size 20
	ServerReaderWriter<Message, Message>* userStream;
	
	User(string u) {
		username = u;
		followers.insert(u);		// new users start following themselves
		following.insert(u);
		userStream = NULL;
	}
};

set<string> active_users;
map<string, User*> existing_users;

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
	
    std::unique_lock<std::mutex> lck(mut);
    
    User* reqUser = existing_users[user];
    if (existing_users.count(followee) == 0) {		// followee doesn't exist
    	reply->set_msg("doesn't exist");
    	return Status::OK;			// can't return different status >:(
    } else if (reqUser->following.count(followee) != 0) {	// already following
    	reply->set_msg("already following");
    	return Status::OK;
    }
    
	User* follUser = existing_users[followee];
	follUser->followers.insert(user);
	reqUser->following.insert(followee);
	reply->set_msg("all good");
    return Status::OK; 
  }

  Status UnFollow(ServerContext* context, const Request* request, Reply* reply) override {
    // ------------------------------------------------------------
    // In this function, you are to write code that handles 
    // request from a user to unfollow one of his/her existing
    // followers
    // ------------------------------------------------------------
    string user = request->username();
    string unfollowee = *(request->arguments().begin());		// user to unfollow
    std::unique_lock<std::mutex> lck(mut);
    
    User* reqUser = existing_users[user];
    if (existing_users.count(unfollowee) == 0) {		// user doesn't exist
    	reply->set_msg("doesn't exist");
    	return Status::OK;
    } else if (reqUser->following.count(unfollowee) == 0) {		// not following user
    	reply->set_msg("invalid");
    	return Status::OK;
    } else if (user == unfollowee) {
    	reply->set_msg("invalid");
    	return Status::OK;
    }
    
    User* unfolUser = existing_users[unfollowee];
    unfolUser->followers.erase(user);
    reqUser->following.erase(unfollowee);
    reply->set_msg("all good");
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
    if (active_users.count(user) != 0)			// user currently active
    	return Status::CANCELLED;
    
    active_users.insert(user);
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
    std::unique_lock<std::mutex> lck(mut);
    Message msg;
    stream->Read(&msg);
    
    string user = msg.username();
    User* u = existing_users[user];
    u->userStream = stream;
    for (const Message& time_msg : u->timeline) {
    	stream->Write(time_msg);
    }
    
    lck.unlock();
    while (stream->Read(&msg)) {
    	lck.lock();
    	for (string followerStr : u->followers) {
    		User* follower = existing_users[followerStr];
    		follower->timeline.push_back(msg);
    		if (follower->userStream != nullptr && followerStr != user) {
    			follower->userStream->Write(msg);
    		}
    	}
    	lck.unlock();
    }
    
    lck.lock();
    active_users.erase(user);		// current user is no longer active
    u->userStream = NULL;			// invalidate stream
    
    return Status::OK;
  }
 
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

void terminationHandler(int sig) {
	std::ofstream outfile("users.txt", std::ios::trunc);
	outfile << existing_users.size() << endl;
	
	for (auto it = existing_users.begin(); it != existing_users.end(); it++) {
		User* user = it->second;
		outfile << it->first << endl;		// write user's name
		
		std::ofstream userout(it->first+".txt", std::ios::trunc);		// file for individual user's information

		// write timeline to file
		userout << user->timeline.size() << endl;
		for (auto& msg : user->timeline) {
			auto t = msg.timestamp().seconds();
			userout << msg.username() << " " << msg.msg() << " " << t << endl;
		}
		
		// write followers to file
		userout << user->followers.size() << endl;
		for (auto& followerName : user->followers) userout << followerName << endl;
		
		// write following to file
		userout << user->following.size() << endl;
		for (auto& followingName : user->following) userout << followingName << endl;
		
		delete it->second;		// free allocated User struct
		userout.close();
	}
	
	outfile.close();
	exit(1);
}

int main(int argc, char** argv) {
	struct sigaction interruptHandler;
	interruptHandler.sa_handler = terminationHandler;
	sigemptyset(&interruptHandler.sa_mask);
	interruptHandler.sa_flags = 0;
	
	sigaction(SIGINT, &interruptHandler, NULL);

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
