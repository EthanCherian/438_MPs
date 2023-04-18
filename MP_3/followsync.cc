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


class SNSFollowSyncImpl final : public SNSFollowSync::Service {
    Status SyncUsers(ServerContext* context, const Users* users, Reply* reply) override {

    }

    Status SyncRelations(ServerContext* context, const Relation* relation, Reply* reply) override {

    }

    Status SyncTimeline(ServerContext* context, const Post* post, Reply* reply) override {

    }
};

void RunFollowSync(std::string port) {
    
}

int main(int argc, char** argv) {
    
}