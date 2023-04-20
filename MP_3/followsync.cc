#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include <fstream>
#include <glog/logging.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <sstream>
#include <memory>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unordered_set>
#include <filesystem>
#include <unistd.h>
#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "snsFollowSync.grpc.pb.h"
#include "snsCoordinator.grpc.pb.h"
#include "helpers.h"

using grpc::Status;
using grpc::ServerContext;
using grpc::ClientContext;
using grpc::ServerBuilder;
using grpc::ClientReaderWriter;
using snsCoordinator::SNSCoordinator;
using snsCoordinator::Heartbeat;
using snsCoordinator::ServerType;
using snsCoordinator::FollowSyncs;
using snsFollowSync::Users;
using snsFollowSync::Reply;
using snsFollowSync::Relation;
using snsFollowSync::Post;
using snsFollowSync::SNSFollowSync;

std::unique_ptr<SNSCoordinator::Stub> coordinator_stub_;
std::vector<std::unique_ptr<SNSFollowSync::Stub>> fsyncs(3);

std::unordered_set<std::string> directoryInfo;
std::string base_path;

std::unordered_set<int> allUsers;
std::unordered_map<int, std::vector<int>> followers;
// followers[1] :: all people user 1 is followed by

std::string coord_ip;
std::string coord_port;
int clusterId;
std::string port;

class SNSFollowSyncImpl final : public SNSFollowSync::Service {
    Status SyncUsers(ServerContext* context, const Users* users, Reply* reply) override {
        //
    }

    Status SyncRelations(ServerContext* context, const Relation* relation, Reply* reply) override {
        std::string filename = std::to_string(relation->follower()) + "_followers";
        std::string filePath = directory.string() + filename + ".txt";
        file_lock(filename);
        std::ofstream file(filePath);
        if (file.is_open()) {
            log(INFO, "Cluster" << clusterId << "writing followers to " << filePath);
            file << relation->followee() << " " << relation->follow_time() << std::endl;
        }
        file_unlock(filename);
    }

    Status SyncTimeline(ServerContext* context, const Post* post, Reply* reply) override {
        //
    }
};

// thread function to continually check list of all users
void checkAllUsers() {
    std::string users_name = "all_users";
    std::string users_path = truePath("all_users.txt");

    while (true) {
        log(INFO, "Cluster" << clusterId << "checking all users file");
        if (hasFileChanged(users_path)) {
            log(INFO, "Cluster" << clusterId << "found new changes");
            Users users;

            file_lock(users_name);
            std::string line;
            std::ifstream file(users_path);
            while (std::getline(file, line)) {
                if (allUsers.count(stoi(line)) == 0) {
                    users.add_user_id(stoi(line));
                    log(INFO, "Cluster" << clusterId << "added user " << line);
                    allUsers.insert(stoi(line));
                }
            }
            file_unlock(users_name);

            ClientContext context;
            Reply reply;
            for (unsigned int i = 0; i < 3; i++) {
                if (i != clusterId)
                    fsyncs.at(i)->SyncUsers(&context, users, &reply);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));           // wait before checking again
    }
}

// thread function to continually check all follow relations
void checkRelations() {
    // std::string relations_path = base_path + std::to_string(userId) + "_relations.txt";

    while (true) {
        //
        std::this_thread::sleep_for(std::chrono::seconds(10));           // wait before checking again
    }
}

// thread function to continually check user timelines
/*
void checkTimeline(int userId) {
    // path to user's timeline
    std::string timeline_path = base_path + std::to_string(userId) + "_timeline.txt";
    
    // compute the last modification time of the timeline
    std::filesystem::file_time_type last_mod = getLastWriteTime(timeline_path);
    directoryInfo = getDirectoryFiles(base_path);
    while (true) {
        if (hasFileChanged(timeline_path, last_mod)) {      // if timeline has changed since last check
            log(INFO, "user" << userId << "detected timeline change");
            last_mod = getLastWriteTime(timeline_path);     // update last modification time

            // isolate new content of timeline
            std::string line, newContent;
            std::ifstream file(timeline_path);
            while (std::getline(file, line)) {
                std::filesystem::file_time_type line_time = getLastWriteTime(line);
                if (line_time > last_mod) newContent += line + "\n";
            }

            // parse each new post, and send messages to corresponding synchronizers
            std::stringstream newposts(newContent);
            std::vector<Post> posts;
            while (std::getline(newposts, line)) {
                std::stringstream ss(line);

                std::string time_str;
                ss >> time_str;

                int poster_id;
                ss >> poster_id;

                std::string content;
                ss >> content;

                google::protobuf::Timestamp ts;
                snsCoordinator::Users new_users;
                for (int follower : followers.at(poster_id)) {
                    google::protobuf::util::TimeUtil::FromString(time_str, &ts);        // convert string back to Timestamp
                    
                    Post p;                         // build post for follower
                    p.set_allocated_timestamp(&ts);
                    p.set_follower_user(follower);
                    p.set_posted_by(poster_id);
                    p.set_msg(content);
                    
                    new_users.add_users(follower);
                    posts.push_back(p);             // add to vector of new posts
                }

                FollowSyncs* sync_servers;
                ClientContext context;
                coordinator_stub_->GetFollowSyncsForUsers(&context, new_users, sync_servers);

                Reply reply;
                for (int i = 0; i < sync_servers->users_size(); i++) {
                    int cluster = sync_servers->follow_syncs().at(i);
                    if (cluster != clusterId)
                        fsyncs.at(cluster)->SyncTimeline(&context, posts.at(i), &reply);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));           // wait before checking again
    }
}
*/
void RunFollowSync(std::string cip, std::string cp, std::string id, std::string p) {
    std::string server_address("localhost:" + port);
    SNSFollowSyncImpl fsync_service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&fsync_service);
    std::unique_ptr<grpc::Server> fsync(builder.BuildAndStart());

    std::cout << "FollowSync server listening on " << server_address << std::endl;
    log(INFO, "FollowSync server listening on " + server_address);

    std::string coord_info = coord_ip + ":" + coord_port;
    coordinator_stub_ = std::unique_ptr<SNSCoordinator::Stub>(
        SNSCoordinator::NewStub(
		grpc::CreateChannel(
			coord_info, grpc::InsecureChannelCredentials()))
    );
    ClientContext con;

    // write initial heartbeat to coordinator, registering follower synchronizer
    Heartbeat hb = MakeHeartbeat(ServerType::SYNC, stoi(id), p);
    std::shared_ptr<ClientReaderWriter<Heartbeat, Heartbeat>> stream(
          coordinator_stub_->HandleHeartBeats(&con));
    stream->Write(hb);

    base_path = "master_"+id+"/";           // synchronizer reads from master directory
    directoryInfo = getDirectoryFiles(base_path);

    std::thread allUserChecker(checkAllUsers);
    allUserChecker.detach();

    std::thread relationChecker(checkRelations);
    relationChecker.detach();

    // TODO: make sure this won't break shit lol
    // for (int u : allUsers) {        // create threads to check each existing user's timeline
    //     std::thread timelineChecker(checkTimeline, u);
    //     timelineChecker.detach();
    // }

    fsync->Wait();
}

int main(int argc, char** argv) {
    std::string cip = "8.8.8.8";
    std::string cp = "8080";
    std::string id = "default";
    std::string p = "3010";
    
    int opt = 0;
    while ((opt = getopt(argc, argv, "cip:cp:p:id:")) != -1){
        switch(opt) {
            case 'cip':
                cip = optarg;break;
            case 'id':
                id = optarg;break;
            case 'cp':
                cp = optarg;break;
            case 'p':
                p = optarg;break;
            default:
                std::cerr << "Invalid Command Line Argument\n";
        }
    }

    coord_ip = cip;
    coord_port = cp;
    clusterId = stoi(id);
    port = p;

    serverId = stoi(id);
    type = ServerType::SYNC;

    RunFollowSync(cip, cp, id, p);
}