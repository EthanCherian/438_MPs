#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <fstream>
#include <glog/logging.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "snsFollowSync.grpc.pb.h"

using grpc::Status;
using grpc::ServerContext;
using grpc::ServerBuilder;
using snsFollowSync::Users;
using snsFollowSync::Reply;
using snsFollowSync::Relation;
using snsFollowSync::Post;
using snsFollowSync::SNSFollowSync;

std::unordered_map<int, Users*> followRelations;

class SNSFollowSyncImpl final : public SNSFollowSync::Service {
    Status SyncUsers(ServerContext* context, const Users* users, Reply* reply) override {

    }

    Status SyncRelations(ServerContext* context, const Relation* relation, Reply* reply) override {

    }

    Status SyncTimeline(ServerContext* context, const Post* post, Reply* reply) override {

    }
};

void RunFollowSync(std::string port) {
    std::string server_address("0.0.0.0:" + port);
    SNSFollowSyncImpl fsync_service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&fsync_service);
    std::unique_ptr<grpc::Server> fsync(builder.BuildAndStart());

    std::cout << "FollowSync server listening on " << server_address << std::endl;
    log(INFO, "FollowSync server listening on " + server_address);

    fsync->Wait();
}

int main(int argc, char** argv) {
    std::string coord_ip = "8.8.8.8";
    std::string coord_port = "8080";
    std::string username = "default";
    std::string port = "3010";
    
    int opt = 0;
    while ((opt = getopt(argc, argv, "cip:cp:p:id:")) != -1){
        switch(opt) {
            case 'cip':
                coord_ip = optarg;break;
            case 'id':
                username = optarg;break;
            case 'cp':
                coord_port = optarg;break;
            case 'p':
                port = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    RunFollowSync(port);
}