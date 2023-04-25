#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include "helper.h"
#include <fstream>
#include <glog/logging.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unistd.h>
#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "sns.grpc.pb.h"
#include "snsCoordinator.grpc.pb.h"

using csce438::Reply;
using csce438::SNSService;
using google::protobuf::Timestamp;
using grpc::ClientContext;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;
using snsCoordinator::ActiveStatus;
using snsCoordinator::ClusterId;
using snsCoordinator::FollowSyncs;
using snsCoordinator::Heartbeat;
using snsCoordinator::Server;
using snsCoordinator::ServerType;
using snsCoordinator::SNSCoordinator;
using snsCoordinator::User;
using snsCoordinator::Users;

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
using std::string;
using std::thread;
using std::unique_lock;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

/*
{
	1: {master1, slave1, fsync1},
	2: ...
	3: ...
}
*/
struct Cluster {
	Server* master;
	Server* slave;
	Server* fsync;
};
unordered_map<int, Cluster*> clusters;
// unordered_map<int, bool> userStatus;

unique_ptr<SNSService::Stub> connectToServer(Server* server) {
	string connect_info = server->server_ip() + ":" + server->port_num();

	return unique_ptr<SNSService::Stub>(SNSService::NewStub(
		grpc::CreateChannel(
			connect_info, grpc::InsecureChannelCredentials())));
}

void CheckHeartbeats() {
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(10));

		log(INFO, "Checking cluster statuses ...");

		for (int i = 1; i <= 3; i++) {
			const Server* master = clusters[i]->master;
			const Server* slave = clusters[i]->slave;

			// check master status
			if (master->active_status() == ActiveStatus::ACTIVE &&
				chronoGetNow() - convertToTimePoint(master->last_heartbeat()) > std::chrono::seconds(20)) {
				// swap master and slave (if active)
				if (slave->active_status() == ActiveStatus::INACTIVE) {
					continue;
				}

				// copy slave to master
				clusters[i]->master = clusters[i]->slave;
				clusters[i]->master->set_server_type(ServerType::MASTER);
				clusters[i]->slave = new Server();

				// send heartbeat to new master to take over master directory, tell it to become master
				auto server_stub = connectToServer(clusters[i]->master);
				ClientContext context;
				Heartbeat hb;
				hb.set_server_type(ServerType::MASTER);

				google::protobuf::Empty empty;
				server_stub->MakeMaster(&context, hb, &empty);
			}

			// check slave status
			if (slave->active_status() == ActiveStatus::ACTIVE &&
				chronoGetNow() - convertToTimePoint(slave->last_heartbeat()) > std::chrono::seconds(10)) {
				// set slave to inactive
				clusters[i]->slave->set_active_status(ActiveStatus::INACTIVE);
			}

			log(INFO, "Cluster: " << i << " Master status: " << clusters[i]->master->active_status() << " Slave status: " << clusters[i]->slave->active_status());
		}
	}
}

class SNSCoordinatorImpl final : public SNSCoordinator::Service {

	Status HandleHeartBeats(ServerContext* context, ServerReaderWriter<Heartbeat, Heartbeat>* stream) override {
		Heartbeat hb;

		bool first = true;

		while (stream->Read(&hb)) {
			log(INFO, "Heartbeats: Received heartbeat from server " << hb.server_id() << " of type " << hb.server_type());

			int cluster_id = hb.server_id();

			Server* server = nullptr;
			switch (hb.server_type()) {
				case ServerType::MASTER:
					server = clusters[cluster_id]->master;

					if (!first)
						break;

					// check if a master is already active
					if (clusters[cluster_id]->master->active_status() == ActiveStatus::ACTIVE) {

						// server is a new slave
						server = clusters[cluster_id]->slave;

						// send back heartbeat to update server to a slave
						hb.set_server_type(ServerType::SLAVE);

						// send new slave info to existing master
						auto master_stub = connectToServer(clusters[cluster_id]->master);
						ClientContext m_context;
						Reply m_reply;
						master_stub->SendSlave(&m_context, hb, &m_reply);
					}
					stream->Write(hb);
					first = false;
					break;
				case ServerType::SLAVE:
					server = clusters[cluster_id]->slave;

					// only write once
					if (!first)
						break;

					stream->Write(hb);
					first = false;

					break;
				case ServerType::SYNC:
					server = clusters[cluster_id]->fsync;
					break;
				default:
					break;
			}
			if (server == nullptr) {
				continue;
			}

			// populate clusters map
			updateServerInCluster(server, hb);

			if (server->server_type() != ServerType::SYNC || !first) {
				continue;
			}

			// send back heartbeats about other two fsync servers to this fsync server
			auto server_stub = connectToServer(server);
			for (auto& cluster : clusters) {
				// make sure this fsync is available
				while (cluster.second->fsync->active_status() == ActiveStatus::INACTIVE) {
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}

				if (cluster.second->fsync->server_id() == server->server_id()) {
					continue;
				}

				makeHeartBeatWithServer(cluster.second->fsync, hb);
				stream->Write(hb);

				log(INFO, "Heartbeats: Sent fsync " << cluster.second->fsync->server_id() << " info to fsync " << server->server_id());
			}
			first = false;
			break;
		}
		return Status::OK;
	}

	Status GetFollowSyncsForUsers(ServerContext* context, const Users* users, FollowSyncs* fsyncs) override {
		log(INFO, "Serving GetFollowSyncsForUsers request");

		for (auto user : users->users()) {
			int clusterId = (user % 3);
			if (clusterId == 0) {
				clusterId = 3;
			}

			Server* fsync = clusters[clusterId]->fsync;
			fsyncs->add_users(user);
			fsyncs->add_follow_syncs(clusterId);
			fsyncs->add_follow_sync_ip(fsync->server_ip());
			fsyncs->add_port_nums(fsync->port_num());
		}
		return Status::OK;
	}

	Status GetServer(ServerContext* context, const User* user, Server* server) override {
		log(INFO, "GetServer called for user " << user->user_id());

		int clusterId = (user->user_id() % 3);
		if (clusterId == 0) {
			clusterId = 3;
		}

		// active master
		if (clusters[clusterId]->master->active_status() == ActiveStatus::ACTIVE) {
			server->CopyFrom(*(clusters[clusterId]->master));

			log(INFO, "GetServer returned to " << user->user_id() << " " << server->server_ip() << ":" << server->port_num());
		} else {
			log(ERROR, "GetServer: Requested cluster " << clusterId << " not active");

			return Status(StatusCode::NOT_FOUND, "cluster not active");
		}

		return Status::OK;
	}

	Status GetSlave(ServerContext* context, const ClusterId* cluster_id, Server* server) override {
		log(INFO, "GetSlave called for master " << cluster_id->cluster());

		// wait for slave to be active
		while (clusters[cluster_id->cluster()]->slave->active_status() == ActiveStatus::INACTIVE) {
			log(INFO, "Waiting for slave to become active");

			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		server->CopyFrom(*(clusters[cluster_id->cluster()]->slave));

		log(INFO, "Return slave info to master " << cluster_id->cluster() << " => " << server->server_ip() << ":" << server->port_num());

		return Status::OK;
	}
};

void RunCoordinator(string port_no) {
	string server_address = "localhost:" + port_no;
	SNSCoordinatorImpl coordinator_service;

	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&coordinator_service);
	unique_ptr<grpc::Server> coordinator(builder.BuildAndStart());
	cout << "Coordinator listening on " << server_address << endl;
	log(INFO, "Coordinator listening on " + server_address);

	// initialize clusters map
	for (int i = 1; i <= 3; i++) {
		clusters[i] = new Cluster();
		clusters[i]->master = new Server();
		clusters[i]->slave = new Server();
		clusters[i]->fsync = new Server();
	}

	// start a thread to check for heartbeats
	thread heart_monitor(CheckHeartbeats);
	heart_monitor.detach();

	coordinator->Wait();
}

int main(int argc, char** argv) {

	string port = "9090";

	int opt = 0;
	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
			case 'p':
				port = optarg;
				break;
			default:
				std::cerr << "Invalid Command Line Argument\n";
		}
	}

	string log_file_name = string("coordinator");
	google::InitGoogleLogging(log_file_name.c_str());
	log(INFO, "Logging Initialized. Coordinator starting...");
	RunCoordinator(port);

	return 0;
}