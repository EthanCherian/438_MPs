#include <glog/logging.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <unordered_map>
#include <string.h>
#include <sstream>
#include <thread>
#include "interface.h"

struct ChatRoom {
    int sockfd, portno, status = 1;
    std::vector<int> connections;

    ChatRoom(int sockfd, int portno) : sockfd{sockfd}, portno{portno} {}

    // Clean up all the connections AND the chatroom socket
    ~ChatRoom() {
        for(int connection: connections) close(connection);
        close(sockfd);
    }

    // Delays the closing of all the chatrooms, instead updating status and sending msg to all connections
    void terminate() { 
        char msg[MAX_DATA] = "Warning: the chatting room is going to be closed...";
        for(int connection: connections) send(connection, msg, MAX_DATA, 0);
        status = 0;
    }

    // Remove connections if client is closed
    void erase_connection(int index) { connections.erase(connections.begin() + index); }

    size_t size() { return connections.size(); }
};

std::unordered_map<std::string, ChatRoom*> chatrooms; // local "database"
int PORTNO = 8081;

sockaddr_in socket_initialization(int sockfd, int port){
    struct sockaddr_in address;
    memset((char*) &address, 0, sizeof(struct sockaddr_in));

    // use initialization from 313
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons( port ); // matches endianess

    if(bind(sockfd, (struct sockaddr *) &address, sizeof(address)) < 0){
        LOG(ERROR) << "bind failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    if(listen(sockfd, 5) < 0){
        LOG(ERROR) << "listen failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    return address;
}

// Function dedicated for threads, spawns new socket to act as chatroom
void chatroom_thread(std::string chatroom_name){
    int sockfd, new_socket;
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) <= 0){
        LOG(ERROR) << "(thread) new socket failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address = socket_initialization(sockfd, PORTNO);
    int addr_len = sizeof(address);
    fd_set readfds;

    ChatRoom chatroom(sockfd, PORTNO++);
    chatrooms[chatroom_name] = &chatroom;
    // chatrooms.emplace(chatroom_name, ChatRoom(sockfd, PORTNO++)); // ask why this would've been a problem
    while(chatroom.status){
        struct timeval tv; // time intervals of 4 seconds, also gives clients ample warning time
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        int max_sockfd = sockfd;
        FD_ZERO(&readfds);
  		FD_SET(sockfd, &readfds);

        for(int conn: chatroom.connections) {
            FD_SET(conn, &readfds);
            max_sockfd = std::max(max_sockfd, conn);
        }

        if(select(max_sockfd + 1, &readfds, NULL, NULL, &tv) < 0){
            LOG(ERROR) << "(thread) select failed" << std::endl;
            LOG(ERROR) << errno << std::endl;
            exit(EXIT_FAILURE);
        }

        if(FD_ISSET(sockfd, &readfds)){ // we know it's an incoming connection
            if( (new_socket = accept(sockfd, (struct sockaddr *) &address, (socklen_t *)&addr_len)) < 0 ){
                LOG(ERROR) << "(thread) accept failed" << std::endl;
                LOG(ERROR) << "(thread) sockfd: " << sockfd << std::endl;
                
                exit(EXIT_FAILURE);
            }
            chatroom.connections.push_back(new_socket);
        } else{ // we know that one of the connections triggered it
            char buf[MAX_DATA];
            int sender_sockfd = -1;
            for(int i = chatroom.size() - 1; i >= 0; i--){
                int conn = chatroom.connections[i];
                if(FD_ISSET(conn, &readfds)){
                    if(read(conn, buf, MAX_DATA) == 0){ // client side died
                        LOG(INFO) << "(thread) client has disconnected..." << std::endl;
                        close(conn);
                        chatroom.erase_connection(i);
                    } else sender_sockfd = conn;
                    break;
                }
            }
            if(sender_sockfd != -1){ // ensure that one of the connections truly triggered it
                for(int conn: chatroom.connections){
                    if(conn != sender_sockfd) send(conn, buf, MAX_DATA, 0);
                }
            }
        }
    }
    chatrooms.erase(chatroom_name); // remove it from map when chatroom closes, also destructs object
    return;
}

int main(int argc, char *argv[]){
    google::InitGoogleLogging(argv[0]);

    LOG(INFO) << "Starting Server";

    int master_socket, client_socket;
    std::string word;

    if((master_socket = socket(AF_INET, SOCK_STREAM, 0)) <= 0){
        LOG(ERROR) << "(main) socket failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address = socket_initialization(master_socket, atoi(argv[1]));
    int addr_len = sizeof(address);
    
    std::vector<int> connections;
    fd_set readfds;
    while(true){
        FD_ZERO(&readfds);
  		FD_SET(master_socket, &readfds);
        char buf[MAX_DATA], msg[MAX_DATA];

        int bytes, maxsock = master_socket;
        for(auto conn: connections){
            FD_SET(conn, &readfds);
            maxsock = std::max(maxsock, conn);
        }

        if(select(maxsock + 1, &readfds, NULL, NULL, NULL) < 0){
            LOG(ERROR) << "(main) select failed" << std::endl;
            LOG(ERROR) << errno << std::endl;
            exit(EXIT_FAILURE);
        }

        if(FD_ISSET(master_socket, &readfds)){
            if( (client_socket = accept(master_socket, (struct sockaddr *) &address, (socklen_t *)&addr_len)) < 0 ){
                LOG(ERROR) << "accept failed" << std::endl;
                exit(EXIT_FAILURE); // might need to exit more gracefully
            }
            connections.push_back(client_socket);
        } else{
            for(int i = connections.size()-1; i >= 0; i--){
                if(!FD_ISSET(connections[i], &readfds)) continue;
                if((bytes = read(connections[i], &buf, MAX_DATA)) < 0){
                    LOG(ERROR) << "server read failed" << std::endl;
                    exit(EXIT_FAILURE); // might need to exit more gracefully
                } else if(bytes == 0) { // client terminated
                    LOG(ERROR) << "server read 0 bytes, maybe client disconnected?" << std::endl;
                    close(connections[i]);
                    connections.erase(connections.begin() + i);
                    continue;
                }

                // Process the command
                std::stringstream ss(buf);
                int command, index = 0;
                while(ss >> word){
                    if(index == 0){
                        if(word == "create") command = 0;
                        else if(word == "join") command = 1;
                        else if(word == "delete") command = 2;
                        else command = 3; // list
                    }
                    index++;
                }

                Reply reply;
                switch(command){
                    case 0: // create
                        // check if chatroom exists
                        // if not, create new chat room socket
                        // add chat room name to map
                        // send the result back to client
                        if(chatrooms.count(word) == 0){
                            std::thread new_thread(chatroom_thread, word);
                            new_thread.detach();
                            reply.status = SUCCESS;
                        } else reply.status = FAILURE_ALREADY_EXISTS;
                        break;
                    case 1: // join
                        if(chatrooms.count(word) == 1){
                            auto& chatroom = chatrooms[word];
                            reply.status = SUCCESS;
                            reply.num_member = chatroom->size();
                            reply.port = chatroom->portno;
                        } else reply.status = FAILURE_NOT_EXISTS;
                        break;
                    case 2: // delete
                        // Check if exist
                        // Send warning message to all connected clients
                        // terminate all connections
                        // close the chat room socket
                        // send the result to the client_socket
                        if(chatrooms.count(word) == 1){
                            chatrooms[word]->terminate();
                            reply.status = SUCCESS;
                        } else reply.status = FAILURE_NOT_EXISTS;
                        break;
                    case 3: // list
                        // check if chatrooms exist
                        // true - send client_socket string containing comma delimited list of names
                        // false - send "empty"
                        if(chatrooms.size() > 0){
                            int ind = 0, cnt = 0;
                            for(auto& [name, value]: chatrooms){
                                for(char c: name) {
                                    msg[ind] = c;
                                    ind++;
                                }
                                cnt++;
                                if(cnt <= chatrooms.size() - 1){
                                    msg[ind++] = ',';
                                    msg[ind++] = ' ';
                                }
                            }
                            msg[ind] = '\0';
                        } else strcpy(msg, "empty");
                        reply.status = SUCCESS;
                        memcpy(reply.list_room, msg, sizeof(msg));
                        break;
                    default:
                        reply.status = FAILURE_INVALID;
                }

                if(send(connections[i], (char*)&reply, MAX_DATA, 0) < 0){
                    LOG(ERROR) << "(main) send failed" << std::endl;
                    exit(EXIT_FAILURE);
                }
                if(close(connections[i]) < 0){
                    LOG(ERROR) << "(main) close client_socket failed" << std::endl;
                    exit(EXIT_FAILURE);
                }
                connections.erase(connections.begin() + i);
            }

        }
    }

    return 0;
}

