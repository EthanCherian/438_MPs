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

#include "snsCoordinator.grpc.pb.h"

using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using snsCoordinator::ActiveStatus;
using snsCoordinator::ClusterId;
using snsCoordinator::FollowSyncs;
using snsCoordinator::Heartbeat;
using snsCoordinator::Server;
using snsCoordinator::ServerType;
using snsCoordinator::SNSCoordinator;
using snsCoordinator::User;
using snsCoordinator::Users;

struct Cluster {
    Server* master;
    Server* slave;
    Server* fsync;
};

std::unordered_map<int, Cluster*> clusters;

class SNSCoordinatorImpl final : public SNSCoordinator::Service {
    Status HandleHeartBeats(ServerContext* context, ServerReaderWriter<Heartbeat, Heartbeat>* stream) override {
        // Heartbeat hb;
        // while (stream->Read(&hb)) {
        //     log(INFO, hb.DebugString());
        // }
        return Status::OK;
    }

    Status GetFollowSyncsForUsers(ServerContext* context, const Users* users, FollowSyncs* fsyncs) override {
        for (User user : users) {
            int clusterId = (user.user_id() % 3) + 1;
            fsyncs->add_cluster_ids(clusterId);
        return Status::OK;
    }

    Status GetServer(ServerContext* context, const User* user, Server* server) override {
        log(INFO, "GetServer called for user " << user->user_id());
        int clusterId = (user->user_id() % 3) + 1;

        // if master is active, use that; otherwise, use slave
        if(clusters[clusterId]->master->active_status() == ActiveStatus::ACTIVE) {
            server->CopyFrom(*(clusters[clusterId]->master));
        } else {
            server->CopyFrom(*(clusters[clusterId]->slave));
        }

        return Status::OK;
    }

    Status GetSlave(ServerContext* context, const ClusterId* clusterId, Server* server) override {
        log(INFO, "GetSlave called for master " << clusterId->cluster());

        server->CopyFrom(*(clusters[clusterId->cluster()]->slave));
        return Status::OK;
    }
};

void RunCoordinator(std::string port) {
    std::string server_address("0.0.0.0:" + port);
    SNSCoordinatorImpl coord_service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&coord_service);
    std::unique_ptr<grpc::Server> coord(builder.BuildAndStart());

    std::cout << "Coordinator listening on " << server_address << std::endl;
    log(INFO, "Coordinator listening on " + server_address);

    coord->Wait();
}

int main(int argc, char** argv) {
    std::string port = "8080";

    int opt = 0;
    while((opt = getopt(argc, argv, "p:")) != -1) {
        switch(opt) {
            case 'p':
                port = optarg;
                break;
        }
    }
}