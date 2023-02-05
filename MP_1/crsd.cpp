#include <glog/logging.h>
// TODO: Implement Chat Server.

int main(int argc, char *argv[]){
    google::InitGoogleLogging(argv[0]);

    if (argc != 2) {
        LOG(ERROR) << "USAGE: enter port number";
		exit(1);
    }
    // accept port number as CLI argument
    int portno = atoi(argv[1]);

    LOG(INFO) << "Starting Server";

    // loop infinitely
    while (true) {
        // listen on port for a CREATE, DELETE, or JOIN request

        // CREATE:
        //      check for existing chat room, if not:
        //          create new master socket
        //          create an entry for new chatroom in local database
        //          inform client about result

        // JOIN:
        //      check whether chat room exists, if yes:
        //          return port number of master socket of the chat room and current number of members

        // DELETE: 
        //      check whether chat room exists, if yes:
        //          send warning message to all clients
        //          terminate client connections
        //          close master socket
        //          delete entry
        //          inform client of result

        // LIST:
        //      check whether chat rooms exists
        //          if yes:
        //              send string of comma separated list of chat room names
        //          if no:
        //              send string "empty"

    }
}