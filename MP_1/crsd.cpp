#include <glog/logging.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <thread>
// TODO: Implement Chat Server.
#include <unordered_map>
#include <vector>
#include "interface.h"

using std::string; using std::unordered_map; using std::vector;

// https://www.geeksforgeeks.org/socket-programming-in-cc-handling-multiple-clients-on-server-without-multi-threading/
unordered_map<string, struct Chatroom*> chatrooms;
int NEXTPORT;

struct Chatroom {
    int portNum, sockfd, status;
    vector<int> connections;            // sockets of all connections
    Chatroom(int p) : portNum(p), status(1) {}
    Chatroom(int p, int s) : portNum(p), sockfd(s), status(1) {}
};

void splitBySpace(string sentence, vector<string>& words) {
    // split sentence by spaces, place into words
    string word = "";
    for (auto x : sentence) {
        if (x == ' ') {
            words.push_back(word);
            word = "";
        }
        else {
            word = word + x;
        }
    }
}

struct sockaddr_in initializeSocket(int sockfd, int portno) {
    // initialize socket connection, return file descriptor
    struct sockaddr_in server_address;
    memset((char*) &server_address, 0, sizeof(struct sockaddr_in));
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_family = INADDR_ANY;
    server_address.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr*) &server_address, sizeof(server_address) < 0)) {
        LOG(ERROR) << "Failed to bind";
        exit(EXIT_FAILURE);
    }

    // listen for messages on the socket
    listen(sockfd, 5);
    return server_address;
}

void chatroomFunction(string roomname, int portno) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG(ERROR) << "  (chatroom) failed to initialize socket";
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address = initializeSocket(sockfd, portno);

    Chatroom newRoom = Chatroom(portno, sockfd);
    chatrooms.emplace(roomname, &newRoom);      // add chatroom to map

    int newsockfd;
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        int maxsockfd = sockfd;
        for (int fd : chatrooms[roomname]->connections) {
            FD_SET(fd, &readfds);
            maxsockfd = std::max(maxsockfd, fd);
        }

        if(select(maxsockfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            LOG(ERROR) << "  (chatroom) failed to select";
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(sockfd, &readfds)) {
            socklen_t addrlen = sizeof(address);
            newsockfd = accept(sockfd, (struct sockaddr*) &address, &addrlen);
            if (newsockfd < 0) {
                LOG(ERROR) << "  (chatroom) failed to accept incoming connection";
                exit(EXIT_FAILURE);
            }
            chatrooms[roomname]->connections.push_back(newsockfd);
            LOG(INFO) << "  added " << newsockfd << " to room " << roomname;
        } else {            // existing connection sent message

        }
    }
}

int main(int argc, char *argv[]) {
    google::InitGoogleLogging(argv[0]);

    if (argc != 2) {
        LOG(ERROR) << "USAGE: enter port number";
		exit(1);
    }
    // accept port number as CLI argument
    int portno = atoi(argv[1]);
    NEXTPORT = portno + 1;

    // establish connection to socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG(ERROR) << "Failed to create socket";
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in client_address = initializeSocket(sockfd, portno);
    socklen_t clilen = sizeof(client_address);

    LOG(INFO) << "Starting Server";

    // loop infinitely
    while (true) {
        int newsockfd = accept(sockfd, (struct sockaddr*) &client_address, &clilen);
        if (newsockfd < 0) {
            LOG(ERROR) << "Failed to accept";
        }

        // listen on port for a CREATE, DELETE, or JOIN request
        char buf[MAX_DATA];
        read(newsockfd, buf, MAX_DATA);
        // touppercase(buf, strlen(buf) - 1);           // TODO: make sure this applies uniformly, but not to names
        string bufstr(buf);
        vector<string> command;
        splitBySpace(bufstr, command);

        // CREATE:
        if (command.at(0) == "CREATE") {
            //  check for existing chat room, if not:
            //      create new master socket
            //      create an entry for new chatroom in local database
            //      inform client about result
            string roomname = command.at(1);
            auto res = chatrooms.find(roomname);
            if (res == chatrooms.end()) {       // chat room doesn't exist, make it
                std::thread chatThread(chatroomFunction, roomname, NEXTPORT++);
                chatThread.detach();
            } else {
                LOG(ERROR) << "Room with name \'" << roomname << "\' already exists";
            }
        }

        // JOIN:
        else if (command.at(0) == "JOIN") {
            //  check whether chat room exists, if yes:
            //      return port number of master socket of the chat room and current number of members
            string roomname = command.at(1);
        }

        // DELETE: 
        else if (command.at(0) == "DELETE") {
            //  check whether chat room exists, if yes:
            //      send warning message to all clients
            //      terminate client connections
            //      close master socket
            //      delete entry
            //      inform client of result
            string roomname = command.at(1);
            chatrooms[roomname]->status = 0;         // set chatroom with given name as inactive
        }

        // LIST:
        else if (command.at(0) == "LIST") {
            //  check whether chat rooms exists
            //      if yes:
            //          send string of comma separated list of chat room names
            //      if no:
            //          send string "empty"
            if (chatrooms.empty()) {
                char ret[] = "empty";
            } else {

            }
        }

    }
}

