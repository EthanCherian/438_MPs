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

unordered_map<string, struct Chatroom*> chatrooms;          // global map tracking all chatrooms
int NEXTPORT;                                               // globally track port to be used for next chatroom

// model relevant information about a chatroom
struct Chatroom {
    int portNum, sockfd, status;
    vector<int> connections;            // sockets of all connections
    Chatroom(int p, int s) : portNum(p), sockfd(s), status(1) {}
    ~Chatroom() {
        for (int conn : connections) close(conn);           // close all connections
        close(sockfd);                                      // close main socket for chatroom
    }

    size_t size() { return connections.size(); }

    void terminate() {
        char warning[MAX_DATA] = "Warning: the chatting room is going to be closed...";
        for (int conn : connections) send(conn, warning, MAX_DATA, 0);      // warn all connections of impending closure
        status = 0;
    }

    void eraseConnection(int index) { connections.erase(connections.begin() + index); }
};

void splitBySpace(string sentence, vector<string>& words) {
    // split sentence by spaces, place into words
    string word = "";
    for (char x : sentence) {
        if (x == ' ') {
            words.push_back(word);
            word = "";
        }
        else {
            if (words.size() == 0) x = toupper(x);          // make first word uppercase
            word = word + x;
        }
    }
    words.push_back(word);
}

struct sockaddr_in initializeSocket(int sockfd, int portno) {
    // initialize socket connection, return file descriptor
    struct sockaddr_in address;
    memset((char*) &address, 0, sizeof(struct sockaddr_in));
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = INADDR_ANY;
    address.sin_port = htons(portno);

    // allow reusing of local addresses so testing is easier
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(sockfd, (struct sockaddr*) &address, sizeof(address)) < 0) {
        LOG(ERROR) << "Failed to bind " << portno;
        exit(EXIT_FAILURE);
    }

    // listen for messages on the socket
    if (listen(sockfd, 5) < 0) {
        LOG(ERROR) << "Failed to listen for connections";
        exit(EXIT_FAILURE);
    }
    return address;
}

void chatroomFunction(string roomname, int portno) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG(ERROR) << "  (chatroom) failed to initialize socket";
        exit(EXIT_FAILURE);
    }
    // initialize main chatroom socket
    struct sockaddr_in address = initializeSocket(sockfd, portno);

    // create new chatroom
    Chatroom newRoom = Chatroom(portno, sockfd);
    chatrooms.emplace(roomname, &newRoom);      // add chatroom to map

    int newsockfd;
    while (newRoom.status > 0) {        // continue as long as chatroom is active
        fd_set readfds;                 // set of file descriptors to be read from
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        int maxsockfd = sockfd;
        for (int fd : chatrooms[roomname]->connections) {       // add all connection sockets to set
            FD_SET(fd, &readfds);
            maxsockfd = std::max(maxsockfd, fd);                // keep track of highest file descriptor
        }

        struct timeval tv;
        tv.tv_sec = 4;
        tv.tv_usec = 0;
        // wait for new bytes to be written to socket from fd set, timeout after 4 seconds
        if(select(maxsockfd + 1, &readfds, NULL, NULL, &tv) < 0) {
            LOG(ERROR) << "  (chatroom) failed to select";
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(sockfd, &readfds)) {       // a new connection sent the message
            socklen_t addrlen = sizeof(address);
            newsockfd = accept(sockfd, (struct sockaddr*) &address, &addrlen);      // accept the connection
            if (newsockfd < 0) {
                LOG(ERROR) << "  (chatroom) failed to accept incoming connection";
                exit(EXIT_FAILURE);
            }
            chatrooms[roomname]->connections.push_back(newsockfd);                  // add to chatroom
        } else {            // existing connection sent message
            char buf[MAX_DATA];
            int clientfd = -1;
            for (int i = 0; i < newRoom.size(); i++) {
                int conn = newRoom.connections[i];
                if (FD_ISSET(conn, &readfds)) {
                    if (read(conn, buf, MAX_DATA) == 0) {            // nothing being read means client died
                        close(conn);                        // close connection from both ends
                        newRoom.eraseConnection(i);
                    } else clientfd = conn;
                    break;
                }
            }
            if (clientfd != -1) {
                for (int conn : newRoom.connections) {
                    if (conn != clientfd) send(conn, buf, MAX_DATA, 0);     // send to all other users
                }
            }
        }
    }

    chatrooms.erase(roomname);              // remove chatroom from system
    return;
}

int main(int argc, char *argv[]) {
    google::InitGoogleLogging(argv[0]);

    if (argc != 2) {
        LOG(ERROR) << "USAGE: enter port number";
		exit(1);
    }
    // accept port number as CLI argument
    int portno = atoi(argv[1]);
    NEXTPORT = portno + 1;              // chatrooms' port numbers should start immediately after main server's

    // establish connection to socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG(ERROR) << "Failed to create socket";
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in client_address = initializeSocket(sockfd, portno);       // move bulk of socket work into function
    socklen_t clilen = sizeof(client_address);

    LOG(INFO) << "Starting Server";

    // loop infinitely
    while (true) {
        // accept client's incoming connection, store socket file descriptor
        int newsockfd = accept(sockfd, (struct sockaddr*) &client_address, &clilen);
        if (newsockfd < 0) {
            LOG(ERROR) << "Failed to accept";
            exit(EXIT_FAILURE);
        }

        // listen on port for a CREATE, DELETE, or JOIN request
        char buf[MAX_DATA];
        int bytes = read(newsockfd, buf, MAX_DATA);     // read input into buffer
        if (bytes < 0) {
            LOG(ERROR) << "Failed to read";
            exit(EXIT_FAILURE);
        } else if (bytes == 0) {                // read nothing from client
            LOG(ERROR) << "Read nothing, did client disconnect?";
        }
        string bufstr(buf);
        vector<string> command;
        splitBySpace(bufstr, command);      // split input by space to separate command from name

        Reply reply;
        // CREATE:
        if (command.at(0) == "CREATE") {
            //  check for existing chat room, if not:
            //      create new master socket
            //      create an entry for new chatroom in local database
            //      inform client about result
            string roomname = command.at(1);
            auto res = chatrooms.find(roomname);
            if (res == chatrooms.end()) {       // chat room doesn't exist, make it
                std::thread chatThread(chatroomFunction, roomname, NEXTPORT++);     // create thread, increase next port to be used
                chatThread.detach();            // thread should run independently
                reply.status = SUCCESS;
            } else {
                reply.status = FAILURE_ALREADY_EXISTS;
            }
        }

        // JOIN:
        else if (command.at(0) == "JOIN") {
            //  check whether chat room exists, if yes:
            //      return port number of master socket of the chat room and current number of members
            string roomname = command.at(1);
            auto res = chatrooms.find(roomname);
            if (res == chatrooms.end()) {           // chat room doesn't exist
                reply.status = FAILURE_NOT_EXISTS;
            } else {
                reply.status = SUCCESS;
                reply.num_member = res->second->connections.size();     // return desired values
                reply.port = res->second->portNum;
            }
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
            auto res = chatrooms.find(roomname);
            if (res == chatrooms.end()) {
                reply.status = FAILURE_NOT_EXISTS;
            } else {
                chatrooms[roomname]->terminate();         // close chatroom
                reply.status = SUCCESS;
            }
        }

        // LIST:
        else if (command.at(0) == "LIST") {
            //  check whether chat rooms exists
            //      if yes:
            //          send string of comma separated list of chat room names
            //      if no:
            //          send string "empty"
            string ret = "";
            reply.status = SUCCESS;
            if (chatrooms.empty()) {
                ret = "empty";
            } else {
                for (auto cr : chatrooms) {         // add all names to string
                    ret += cr.first + ",";
                }
            }
            strcpy(reply.list_room, ret.c_str());   // copy string to reply's char array
        }
        
        else {      // some other command
            reply.status = FAILURE_INVALID;
            LOG(ERROR) << "Unrecognized command";
        }

        char replBuf[MAX_DATA];
        memcpy(replBuf, &reply, sizeof(reply));     // put reply into character buffer

        // send reply to client 
        int sentBytes = send(newsockfd, replBuf, MAX_DATA, 0);
        if (sentBytes < 0) {
            LOG(ERROR) << "Failed to send reply to client";
            exit(EXIT_FAILURE);
        }

        // close client socket on server end
        int closed = close(newsockfd);
        if (closed < 0) {
            LOG(ERROR) << "Failed to close connection to client";
            exit(EXIT_FAILURE);
        }
    }
}

