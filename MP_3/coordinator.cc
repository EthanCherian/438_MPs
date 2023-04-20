#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <fstream>
#include <glog/logging.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <thread>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "snsCoordinator.grpc.pb.h"
#include "helpers.h"

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

std::mutex c_lock;

void CheckHeartbeats() {
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(10));

		std::unique_lock<std::mutex> lock(c_lock);
		for (auto& cluster : clusters) {
			Server* master = cluster.second->master;
			if (master->active_status() == ActiveStatus::ACTIVE &&
				chronoGetNow() - convertToTimePoint(master->last_heartbeat()) > std::chrono::seconds(20)) {
				master->set_active_status(ActiveStatus::INACTIVE);
			}

			auto slave = cluster.second->slave;
			if (slave->active_status() == ActiveStatus::ACTIVE &&
				chronoGetNow() - convertToTimePoint(slave->last_heartbeat()) > std::chrono::seconds(10)) {
				slave->set_active_status(ActiveStatus::INACTIVE);
			}
		}
		lock.unlock();
	}
}

class SNSCoordinatorImpl final : public SNSCoordinator::Service {
    Status HandleHeartBeats(ServerContext* context, ServerReaderWriter<Heartbeat, Heartbeat>* stream) override {
		Heartbeat hb;
		while (stream->Read(&hb)) {
			std::unique_lock<std::mutex> lock(c_lock);
			int cluster_id = hb.server_id();
			if (clusters.find(cluster_id) != clusters.end()) {
				Server* server = nullptr;
				switch (hb.server_type()) {
					case ServerType::MASTER:
						server = clusters[cluster_id]->master;
						break;
					case ServerType::SLAVE:
						server = clusters[cluster_id]->slave;
						break;
					case ServerType::SYNC:
						server = clusters[cluster_id]->fsync;
						break;
					default:
						break;
				}
				if (server != nullptr) {
					server->set_server_ip(hb.server_ip());
					server->set_port_num(hb.server_port());
					server->set_server_id(hb.server_id());
					server->set_server_type(hb.server_type());
					server->set_active_status(ActiveStatus::ACTIVE);
					server->set_allocated_last_heartbeat(protoGetNow());
				}
			}
			lock.unlock();
		}
		return Status::OK;
	}

    Status GetFollowSyncsForUsers(ServerContext* context, const Users* users, FollowSyncs* fsyncs) override {
        for (auto user : users->users()) {      // consider all user ids
            int clusterId = (user % 3) + 1;
            // get appropriate follower synchronizer server
            auto fsync = clusters[clusterId]->fsync;
            // add fields to reply
            fsyncs->add_users(user);
            fsyncs->add_follow_syncs(clusterId);
            fsyncs->add_follow_sync_ip(fsync->server_ip());
            fsyncs->add_port_nums(fsync->port_num());
        }
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

    RunCoordinator(port);
}