#include <ctime>

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>

#include "helper.h"
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <glog/logging.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "snsCoordinator.grpc.pb.h"
#include "snsFollowSync.grpc.pb.h"

using google::protobuf::Timestamp;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using snsCoordinator::FollowSyncs;
using snsCoordinator::Heartbeat;
using snsCoordinator::ServerType;
using snsCoordinator::SNSCoordinator;
using snsFollowSync::Post;
using snsFollowSync::Posts;
using snsFollowSync::Relation;
using snsFollowSync::Reply;
using snsFollowSync::SNSFollowSync;
using snsFollowSync::Users;

using std::begin;
using std::cout;
using std::distance;
using std::end;
using std::endl;
using std::find;
using std::find_if;
using std::fstream;
using std::ifstream;
using std::ios;
using std::istringstream;
using std::map;
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
using std::unordered_set;
using std::vector;

// global variables for this server
string server_ip = "localhost";
string port = "9090";

// stub for current server doesn't exist
vector<unique_ptr<SNSFollowSync::Stub>> fsyncs(3);
unique_ptr<SNSCoordinator::Stub> coordinator_stub;
unordered_set<string> directoryInfo;
string base_path;
mutex mu_;
std::filesystem::path slave_directory;

Heartbeat MakeHeartbeat(ServerType type, int serverId, string port_no) {
	Heartbeat h;
	h.set_server_type(type);
	h.set_server_id(serverId);
	h.set_server_ip("localhost");
	h.set_server_port(port_no);
	google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
	timestamp->set_seconds(time(NULL));
	timestamp->set_nanos(0);
	h.set_allocated_timestamp(timestamp);
	return h;
}

unordered_set<string> getDirectoryFiles(const string& directoryPath) {
	unordered_set<string> files;

	for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
		files.insert(entry.path().filename().string());
	}

	return files;
}

void checkForChanges(const string& directoryPath, unordered_set<string>& previousFiles) {
	auto currentFiles = getDirectoryFiles(directoryPath);

	for (const auto& file : currentFiles) {
		if (previousFiles.find(file) == previousFiles.end()) {
			log(INFO, "Detected file added: " << file);
		}
	}

	for (const auto& file : previousFiles) {
		if (currentFiles.find(file) == currentFiles.end()) {
			log(INFO, "Detected file removed: " << file);
		}
	}

	previousFiles = currentFiles;
}

bool isFollowingFollowerTimelineFile(const string& filename) {
	return filename.find("following") != string::npos || filename.find("followers") != string::npos || filename.find("timeline") != string::npos;
}

// thread function to continually check list of all users
void checkAllUsers() {
	string all_users_file = "all_users";
	string all_users_path = truePath("all_users.txt", ServerType::MASTER);

	// make sure all_users.txt exists
	while (!std::filesystem::exists(all_users_path)) {
		log(INFO, "all_users.txt not found yet...");

		std::this_thread::sleep_for(std::chrono::seconds(2));
	}

	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(10)); // wait before checking again

		if (fileChanged(all_users_path)) {
			file_lock(all_users_file, ServerType::MASTER);
			Users newusers;
			string line;
			ifstream file(all_users_path);
			while (getline(file, line)) {
				if (all_uids.count(stoi(line)) == 0) {
					newusers.add_user_id(stoi(line));
					log(INFO, "Cluster " << server_id << " added user " << line);
					all_uids.insert(stoi(line));
				}
			}
			file_unlock(all_users_file, ServerType::MASTER);

			for (unsigned int i = 0; i < 3; i++) {
				if (i + 1 != stoi(server_id)) {
					log(INFO, "Forwarding to fsync: " << i + 1);

					ClientContext context;
					Reply reply;
					fsyncs.at(i)
						->SyncUsers(&context, newusers, &reply);
				}
			}
		}
	}
}

// thread function to continually check all follow relations
void checkRelations() {
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(15));

		vector<pair<string, string>> filePaths;
		getChangedFiles(filePaths, &isFollowingFollowerTimelineFile, false);

		// Relations datastructure,
		// Change the Relation to include "uint64 follow_time"
		vector<pair<int, pair<string, string>>> changedTimelines;
		for (auto& [filepath, filename] : filePaths) {
			std::ifstream infile(filepath);
			string uid, fileType;
			int tmp, uidInt;
			long long tme;
			istringstream ss(filename);
			getline(ss, uid, '_');
			getline(ss, fileType, '_');
			uidInt = stoi(uid);

			log(INFO, "Sync" << server_id << " found new changes in " << filepath);

			if (fileType == "timeline") {
				changedTimelines.push_back({uidInt, {filename, filepath}});
				continue;
			}

			if (fileType == "following" || fileType == "followers") {
				file_lock(filename, ServerType::MASTER);
				std::unique_lock<std::mutex> lock(mu_);
				if (fileType == "following") {
					Relation relation;
					relation.set_followee(uidInt);
					while (infile >> tmp >> tme) {
						if (infile.eof() && infile.bad()) break;
						if (following[uidInt].count(tmp) == 0) { // new follower

							log(INFO, uidInt << " is now following " << tmp << " at time " << tme);

							ClientContext client_context;
							Reply rep;
							relation.set_follower(tmp);
							relation.set_follow_time(tme);
							fsyncs.at((tmp - 1) % 3)->SyncRelations(&client_context, relation, &rep);
							// TODO: Ensure SyncRelations worked by checking status
						}
						following[uidInt][tmp] = tme;
					}
				} else {
					while (infile >> tmp >> tme) {
						if (infile.eof() && infile.bad()) break;
						followers[uidInt][tmp] = tme;
					}
				}
				file_unlock(filename, ServerType::MASTER);
			}
		}

		// Handle timeline
		map<int, set<string>> TUPE;
		for (auto& [userId, pairString] : changedTimelines) {
			auto [filename, filepath] = pairString;
			file_lock(filename, ServerType::MASTER);
			log(INFO, "Acquired " + filepath + " lock.");
			std::unique_lock<std::mutex> lock(mu_);

			// Read entire userId timeline
			string line;
			std::ifstream infl(filepath);
			while (std::getline(infl, line)) {
				if (infl.eof() && infl.bad()) break;
				if (line == "") continue;

				// iterate over followers[userId]
				log(INFO, "Before followers iteration: " + line);
				for (auto& [folId, folTme] : followers[userId]) {
					google::protobuf::Timestamp timestamp;
					timestamp.set_seconds(folTme);
					string strtime = google::protobuf::util::TimeUtil::ToString(timestamp);
					log(INFO, line);
					log(INFO, strtime);
					log(INFO, "Is timestamp less than line? " << (strtime < line));
					if (strtime < line)
						TUPE[folId].insert(line);
				}
			}

			file_unlock(filename, ServerType::MASTER);
		}

		// Now use the map to write
		for (auto& [fid, newTimeline] : TUPE) {
			ClientContext client_context;
			Reply rep;
			Posts posts;
			posts.set_follower_user(fid);
			for (const auto& message : newTimeline) {
				std::stringstream ss(message);
				string timestamp, separator, user;

				ss >> timestamp >> separator; // get extras first
				std::getline(ss, user, ':');

				// check if person posting is in following[fid] before addding message
				int uid = stoi(user);
				if (followers[uid].count(fid) == 1 && uid != fid)
					posts.add_msgs(message);
			}
			fsyncs.at((fid - 1) % 3)->SyncTimeline(&client_context, posts, &rep);
		}
	}
}

class SNSFollowSyncImpl final : public SNSFollowSync::Service {
	Status SyncUsers(ServerContext* context, const Users* users, Reply* reply) override {
		log(INFO, "Running SyncUsers");

		file_lock("all_users", ServerType::MASTER);
		file_lock("all_users", ServerType::SLAVE);

		std::unique_lock<std::mutex> lock(mu_);
		string user_file = "all_users.txt";
		string user_master_path = truePath(user_file, ServerType::MASTER);
		string user_slave_path = truePath(user_file, ServerType::SLAVE);
		fstream all_users(user_master_path, ios::out | ios::app);
		fstream all_usersS(user_slave_path, ios::out | ios::app);

		for (int i = 0; i < users->user_id_size(); i++) {
			int user_id = users->user_id(i);

			all_users << user_id << endl;
			all_usersS << user_id << endl;
			all_uids.insert(user_id);
		}

		file_unlock("all_users", ServerType::MASTER);
		file_unlock("all_users", ServerType::SLAVE);

		return Status::OK;
	}

	Status SyncRelations(ServerContext* context, const Relation* relation, Reply* reply) override {
		std::string filename = std::to_string(relation->follower()) + "_followers";
		std::string filePath = directory.string() + filename + ".txt";

		// sync master dir
		file_lock(filename, ServerType::MASTER);
		std::ofstream file(filePath, ios::out | ios::app);
		if (file.is_open()) {
			log(INFO, "Sync" << server_id << " writing followers to " << filePath);

			file << relation->followee() << " " << relation->follow_time() << std::endl;
		}
		file_unlock(filename, ServerType::MASTER);

		// sync slave dir
		filePath = slave_directory.string() + filename + ".txt";
		file_lock(filename, ServerType::SLAVE);
		std::ofstream s_file(filePath, ios::out | ios::app);
		if (s_file.is_open()) {
			log(INFO, "Sync" << server_id << " writing followers to " << filePath);

			s_file << relation->followee() << " " << relation->follow_time() << std::endl;
		}
		file_unlock(filename, ServerType::SLAVE);

		return Status::OK;
	}

	Status SyncTimeline(ServerContext* context, const Posts* posts, Reply* reply) override {
		log(INFO, "Running SyncTimeline");
		int userToUpdate = posts->follower_user();

		string timeline_name = to_string(userToUpdate) + "_timeline";
		string timeline_file = timeline_name + ".txt";
		string masterPath = truePath(timeline_file, ServerType::MASTER);
		string slavePath = truePath(timeline_file, ServerType::SLAVE);

		std::unique_lock<std::mutex> lock(mu_);
		file_lock(timeline_name, ServerType::MASTER);
		file_lock(timeline_name, ServerType::SLAVE);

		std::ifstream timelineMi(masterPath);
		set<string> setMi;
		string line;

		// Read in user's current timeline and override it
		while (getline(timelineMi, line)) {
			if (timelineMi.eof() && timelineMi.bad()) break;
			if (line == "") continue;
			setMi.insert(line);
		}
		for (int i = 0; i < posts->msgs_size(); i++) {
			setMi.insert(posts->msgs(i));
		}

		// update userToUpdate's timeline file in both master and slave directories
		std::ofstream masterTimeline(masterPath);
		std::ofstream slaveTimeline(slavePath);
		for (auto& line : setMi) {
			masterTimeline << line << endl;
			slaveTimeline << line << endl;
		}

		file_times[masterPath] = getLastWriteTime(masterPath);
		file_unlock(timeline_name, ServerType::MASTER);
		file_unlock(timeline_name, ServerType::SLAVE);

		return Status::OK;
	}
};

void sendHeartBeat(string cip, string cp) {
	log(INFO, "Sending heartbeat to coordinator at: " + cip + ":" + cp);

	// Connect to coordinator
	string coordinator_info = cip + ":" + cp;
	coordinator_stub = unique_ptr<SNSCoordinator::Stub>(SNSCoordinator::NewStub(
		grpc::CreateChannel(
			coordinator_info, grpc::InsecureChannelCredentials())));

	// setup heartbeat and stream
	ClientContext context;
	shared_ptr<ClientReaderWriter<Heartbeat, Heartbeat>> stream(
		coordinator_stub->HandleHeartBeats(&context));

	Heartbeat hb;
	hb.set_server_id(stoi(server_id));
	hb.set_server_type(type);
	hb.set_server_ip(server_ip);
	hb.set_server_port(port);
	hb.set_allocated_timestamp(protoGetNow());

	// send heartbeat
	stream->Write(hb);
	stream->WritesDone();

	log(INFO, "Receiving info back from coordinator...");

	// wait to recieve other fsync servers data and make stubs
	Heartbeat reply_hb;
	vector<int> mapping = {2, 0, 1};
	int id_counter = stoi(server_id);

	// current sync server
	fsyncs[id_counter - 1] = unique_ptr<SNSFollowSync::Stub>(SNSFollowSync::NewStub(grpc::CreateChannel(server_ip + ":" + port, grpc::InsecureChannelCredentials())));

	// first sync server
	stream->Read(&reply_hb);
	log(INFO, "Read back server_id: " << reply_hb.server_id());

	fsyncs[reply_hb.server_id() - 1] = unique_ptr<SNSFollowSync::Stub>(SNSFollowSync::NewStub(grpc::CreateChannel(reply_hb.server_ip() + ":" + reply_hb.server_port(), grpc::InsecureChannelCredentials())));

	// second sync server
	stream->Read(&reply_hb);
	log(INFO, "Read back server_id: " << reply_hb.server_id());

	fsyncs[reply_hb.server_id() - 1] = unique_ptr<SNSFollowSync::Stub>(SNSFollowSync::NewStub(grpc::CreateChannel(reply_hb.server_ip() + ":" + reply_hb.server_port(), grpc::InsecureChannelCredentials())));

	stream->Finish();
}

void RunFollowSync(string cip, string cp) {
	// send a heartbeat to coordinator
	thread heartbeatThread(sendHeartBeat, cip, cp);
	heartbeatThread.detach();

	// connect service and start fsync
	string server_address("localhost:" + port);
	SNSFollowSyncImpl fsync_service;

	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&fsync_service);
	unique_ptr<Server> fsync(builder.BuildAndStart());

	// assign master file directory
	directory = "MASTER_" + server_id;
	mkdir(("./" + directory.string()).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	directory += "/";

	// assign slave file directory
	slave_directory = "SLAVE_" + server_id;
	mkdir(("./" + slave_directory.string()).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	slave_directory += "/";

	initialize_write_times();
	initialize_localdb(mu_);

	// sync all users
	thread userSync(checkAllUsers);
	userSync.detach();

	// sync all relations
	thread relationSync(checkRelations);
	relationSync.detach();

	cout << "FollowSync server listening on " << server_address << endl;
	log(INFO, "FollowSync server listening on " + server_address);

	fsync->Wait();
}

static struct option long_options[] = {
	{"cip", required_argument, 0, 'c'},
	{"cp", required_argument, 0, 'i'},
	{"p", required_argument, 0, 'p'},
	{"id", required_argument, 0, 'd'},
	{0, 0, 0, 0}};

int main(int argc, char** argv) {
	string cip = "localhost";
	string cp = "9000";
	type = ServerType::SYNC;

	int option_index = 0;
	int opt = 0;
	while ((opt = getopt_long(argc, argv, "c:i:p:d:", long_options, &option_index)) != -1) {
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
			default:
				std::cerr << "Invalid Command Line Argument\n";
				break;
		}
	}

	string log_file_name = string("fsync-") + server_id;
	google::InitGoogleLogging(log_file_name.c_str());
	log(INFO, "Logging Initialized. Synchronizer starting...");

	RunFollowSync(cip, cp);
}
