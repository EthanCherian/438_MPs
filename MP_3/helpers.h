#include <filesystem>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <google/protobuf/util/time_util.h>
#include <google/protobuf/timestamp.pb.h>

#include "snsCoordinator.grpc.pb.h"

using snsCoordinator::Heartbeat;
using snsCoordinator::ServerType;

ServerType type;
int serverId;
std::unordered_map<std::string, time_t> file_times;
std::filesystem::path directory;

std::chrono::system_clock::time_point chronoGetNow() {
	return std::chrono::system_clock::now();
}

google::protobuf::Timestamp* protoGetNow() {
	google::protobuf::Timestamp* now = new google::protobuf::Timestamp();
	now->set_seconds(time(NULL));
	now->set_nanos(0);
	return now;
}

std::chrono::system_clock::time_point convertToTimePoint(const google::protobuf::Timestamp& timestamp) {
	std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(timestamp.seconds());
	tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(timestamp.nanos()));
	return tp;
}

Heartbeat MakeHeartbeat(ServerType type, int serverId, std::string port_no) {
  Heartbeat h;
  h.set_server_type(type);
  h.set_server_id(serverId);
  h.set_server_ip("0.0.0.0");
  h.set_server_port(port_no);
  google::protobuf::Timestamp* timestamp = new google::protobuf::Timestamp();
  timestamp->set_seconds(time(NULL));
  timestamp->set_nanos(0);
  h.set_allocated_timestamp(timestamp);
  return h;
}

std::unordered_set<std::string> getDirectoryFiles(const std::string& directoryPath) {
    std::unordered_set<std::string> files;

    for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
        files.insert(entry.path().filename().string());
    }

    return files;
}

std::unordered_set<std::string> hasDirectoryChanged(const std::string& directoryPath, std::unordered_set<std::string>& previousFiles) {
    auto currentFiles = getDirectoryFiles(directoryPath);
    std::unordered_set<std::string> changedFiles;

    for (const auto& file : currentFiles) {
        if (previousFiles.find(file) == previousFiles.end()) {
            // std::cout << "File added: " << file << std::endl;
            changedFiles.insert(file);
        }
    }

    // for (const auto& file : previousFiles) {
    //     if (currentFiles.find(file) == currentFiles.end()) {
    //         // std::cout << "File removed: " << file << std::endl;
    //         changedFiles.remove(file);
    //     }
    // }

    // previousFiles = currentFiles;
    return changedFiles;
}

std::string truePath(std::string filename){
  std::string typeStr = "master";
  return typeStr + std::to_string(serverId) + "/" + filename;
}

// Block the the calling thread so long as the file exists
void file_lock(std::string filename){
  std::string path = truePath(filename)+"_lock";
  while(access(path.c_str(), F_OK) == 0){
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  std::ofstream outfile(path); // create the file to "lock"
}

void file_unlock(std::string filename){
  std::string path = truePath(filename)+"_lock";
  if(remove(path.c_str()) != 0) { log(INFO, "unable to delete " + path); }
  else { log(INFO, path+" successfully deleted."); }
}

time_t getLastWriteTime(const std::string& filePath) {
    struct stat buf;
    stat(filePath.c_str(), &buf);
    return buf.st_mtime;
}

bool hasFileChanged(const std::string& filePath) {
    auto currentWriteTime = getLastWriteTime(filePath);
    if (currentWriteTime != file_times[filePath]) {
        file_times[filePath] = currentWriteTime;
        return true;
    }
    return false;
}

void initialize_write_times(){
    std::vector<std::pair<std::string, std::string>> filePaths;
    for (auto const& file: std::filesystem::directory_iterator{directory}) {
        std::string filepath = file.path();
        auto tmp = getLastWriteTime(filepath);
        file_times[filepath] = getLastWriteTime(filepath);
    }
}