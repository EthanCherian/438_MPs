#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <glog/logging.h>
#include <google/protobuf/duration.pb.h>
#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>
#include <iostream>
#include <set>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#define log(severity, msg) \
	LOG(severity) << msg;  \
	google::FlushLogFiles(google::severity);

#include "snsCoordinator.grpc.pb.h"

using google::protobuf::Timestamp;
using snsCoordinator::ActiveStatus;
using snsCoordinator::Heartbeat;
using snsCoordinator::ServerType;

using std::cout;
using std::endl;
using std::istringstream;
using std::map;
using std::mutex;
using std::pair;
using std::set;
using std::string;
using std::to_string;
using std::unordered_map;
using std::vector;

string server_id = "1";
unordered_map<string, time_t> file_times;
std::filesystem::path directory;
ServerType type = ServerType::NONE;

set<int> all_uids; // map that stores all the users
unordered_map<int, map<int, long long>> followers; // uid, map of follower_id, timestamp
unordered_map<int, map<int, long long>> following;

std::chrono::system_clock::time_point chronoGetNow() {
	return std::chrono::system_clock::now();
}

Timestamp* protoGetNow() {
	Timestamp* now = new Timestamp();
	now->set_seconds(time(NULL));
	now->set_nanos(0);
	return now;
}

std::chrono::system_clock::time_point convertToTimePoint(const Timestamp& timestamp) {
	std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(timestamp.seconds());
	tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(timestamp.nanos()));
	return tp;
}

void updateServerInCluster(snsCoordinator::Server* server, Heartbeat& hb) {
	server->set_server_ip(hb.server_ip());
	server->set_port_num(hb.server_port());
	server->set_server_id(hb.server_id());
	server->set_server_type(hb.server_type());
	server->set_active_status(ActiveStatus::ACTIVE);
	server->set_allocated_last_heartbeat(protoGetNow());
}

void makeHeartBeatWithServer(snsCoordinator::Server* server, Heartbeat& hb) {
	hb.set_server_id(server->server_id());
	hb.set_server_ip(server->server_ip());
	hb.set_server_port(server->port_num());
	hb.set_server_type(server->server_type());
	hb.set_allocated_timestamp(protoGetNow());
}

string truePath(string filename, ServerType s_type = ServerType::NONE) {
	s_type = (type == ServerType::SYNC) ? s_type : type;
	string typeStr = snsCoordinator::ServerType_Name(s_type);
	return typeStr + "_" + server_id + "/" + filename;
}

// Block the the calling thread so long as the file exists
void file_lock(string filename, ServerType s_type = ServerType::NONE) {
	string path = truePath(filename, s_type) + "_lock";
	while (std::filesystem::exists(path)) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	std::ofstream outfile(path); // create the file to "lock"
}

void file_unlock(string filename, ServerType s_type = ServerType::NONE) {
	string path = truePath(filename, s_type) + "_lock";
	if (remove(path.c_str()) != 0) {
		log(INFO, "Unable to delete " + path);
	} else {
		log(INFO, path + " successfully deleted.");
	}
}

time_t getLastWriteTime(const std::string& filePath) {
	struct stat buf;
	stat(filePath.c_str(), &buf);
	return buf.st_mtime;
}

bool fileChanged(const std::string& filePath) {
	log(INFO, filePath << ": " << file_times[filePath]);

	if (access(filePath.c_str(), F_OK) == 0) {
		auto currentWriteTime = getLastWriteTime(filePath);
		if (currentWriteTime != file_times[filePath]) {
			file_times[filePath] = currentWriteTime;
			return true;
		}
	}
	return false;
}

void initialize_write_times(std::filesystem::path dir = directory) {
	vector<pair<string, string>> filePaths;
	for (auto const& file : std::filesystem::directory_iterator{dir}) {
		string filepath = file.path();
		cout << filepath << endl;
		auto tmp = getLastWriteTime(filepath);
		file_times[filepath] = getLastWriteTime(filepath);
	}
}

void getChangedFiles(vector<pair<string, string>>& filePaths, bool (*func)(const string&), bool init) {
	for (auto const& file : std::filesystem::directory_iterator{directory}) {
		string filepath = file.path();
		string filename = file.path().stem().string();
		if (func(filename) && (init || fileChanged(filepath)) && filename.find("lock") == string::npos) {

			log(INFO, "file in getChangedFiles: " << filename);

			filePaths.push_back({filepath.c_str(), filename});
		}
	}
}

bool isNotTimeline(const string& filename) {
	return filename.find("timeline") == string::npos;
}

void initialize_localdb(mutex& mu_) {
	vector<pair<string, string>> filePaths;
	getChangedFiles(filePaths, &isNotTimeline, true);

	for (auto& [filepath, filename] : filePaths) {
		file_lock(filename, ServerType::MASTER);
		std::unique_lock<mutex> lock(mu_);
		std::ifstream infile(filepath);

		string uid, fileType;
		int tmp, uidInt;
		long long tme;

		istringstream ss(filename);
		getline(ss, uid, '_');
		getline(ss, fileType, '_');

		if (fileType == "followers") {
			uidInt = stoi(uid);
			while (infile >> tmp >> tme) {
				if (infile.eof() && infile.bad()) break;
				followers[uidInt][tmp] = tme;
			}
		} else if (fileType == "following") {
			uidInt = stoi(uid);
			while (infile >> tmp >> tme) {
				if (infile.eof() && infile.bad()) break;
				following[uidInt][tmp] = tme;
			}
		} else if (fileType == "users") { // part of all_users
			while (infile >> uidInt) {
				if (infile.eof() && infile.bad()) break;
				all_uids.insert(uidInt);
			}
		}
		file_unlock(filename, ServerType::MASTER);
	}
}
