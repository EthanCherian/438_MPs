### Running servers and clients

> make
> ./startup.sh
Runs the coordinator and the servers in the three clusters, with 10 total processes.

 > ./terminate.sh
Kills the 10 servers

 > make clean
Removes all created files and directories

 > make dclean
Removes only the directories for servers and the logs

### Client program design

Similar to client program described in MP2, with the commands and functions described below.

* `connectTo()` creates a gRPC stub connecting to the server's hostname and port, calling `Login()` on the stub to "authenticate" the user
  * First connects to the Coordinator server to get the hostname and port of a master in the cluster
  * `Login()` function can repeatedly ping the Coordinator server to get connect to a master in a cluster, other command functions call `Login()` to ensure connect to a server when a master fails.
* `processCommand()` processes each of the user's available commands and constructs the appropriate `IReply` object
  * `FOLLOW` command: `request` should send usernames and call `Follow()` on the stub
  * `LIST` command: Loop through lists of all users and following users returned from the server's `List()` into `IReply`
  * `TIMELINE` command: should just return a gRPC `OK` status for main function to then call `processTimeline()`
* `processTimeline()` command: create a new `ClientReaderWriter stream` object to both read and write a stream of responses and replies from the server.
  * Writer thread on stream to listen to CLI input and write message to server.
    * Need to initially send user's username to get timeline history
    * Construct a `timestamp` using gRPC's `Timestamp` and `std::time`
  * Reader thread to listen to replies from server and display

### Server program design

The server is described similar to that of MP 2. Each server cluster consists of a master server, a slave server, and a synchronizer. They all share a common clusterId, which serves to logically group each of the three clusters together. The main difference between the master and slave servers is in their communication with clients and fault tolerance responsibilities.

* Both master and slave servers maintain an in-memory store of all users on the system, as well as two total maps for each user's following and followers. This data is updated whenever a change is detected in the corresponding files within their directories.
  * The master server actively monitors the files, while the slave server relies on the master to relay all necessary information.
* Master and slave servers send heartbeat messages to the coordinator every 10 seconds and receive replies. If a server fails to send 2 heartbeat messages in a row, it is marked as inactive.
* When a master server dies, the coordinator informs the corresponding slave, who then becomes a master.

The main functionalities of the server program include:

* RPC functions to handle a slave becoming a master, and an old master starting up as a slave through the coordinator
* `List()`: Get the requesting user's username, and return all users and the user's following list from the data file.
* `Follow()`: Ensure both users exist and aren't already following each other; update their following and followers lists in the data file.
* `Timeline()`: Manage user streams in timeline mode, print the last 20 messages or messages since the user started following, and add written messages to the user's timeline file for the synchronizer to update in other clusters. The slave's stream will be maintained by the master in timeline to forward messages to the slave, and the new messages send to user must be sorted by timestamp.
* `RunServer()`: Start the server using gRPC's API
  * Connects to coordinator to inform of its existence and get the address and port of its slave server.
  * If a master server already exists, it must become a slave
  * Assign a file directory for the server to store its data files
  * Startup threads to handle heartbeat messages and changes to files (from the synchronizer)
  * Master should connect to the slave to get its stub.

### Coordinator program design

The coordinator plays a crucial role in the SNS by managing server clusters, their respective masters and slaves, and ensuring proper communication between clients and servers.

* Upon receiving a client's request, the coordinator assigns the client to a server cluster by calculating the clusterId as (userId % 3) + 1. It maintains an in-memory map associating clusterIds with the corresponding server information.
* The coordinator returns the IP address and port number of the assigned master server to the client, after which there should be no further communication between the client and the coordinator.
* A dedicated thread in the coordinator handles heartbeat messages from all servers (masters and slaves), monitoring their statuses and updating their last recorded heartbeat timestamp.
  * If a server fails to respond to heartbeat messages twice (at least 20s apart), it's marked as inactive.
  * If an inactive server is a master, its corresponding slave is promoted to master, and the directory is updated accordingly. If the inactive server comes back online, it becomes the slave.
  * The synchronizer class sends a single heartbeat message to the coordinator when it comes online, to update the coordinator's in-memory database.
* The coordinator waits for other communications from masters, slaves, or synchronizers.
  * When a master starts up, it asks the coordinator for the address and port of its corresponding slave server, allowing it to forward client requests appropriately.
  * When a synchronizer detects changes to a timeline, it determines which users must learn of the new posts and asks the coordinator for the addresses and ports of each user's assigned synchronizer.

These tasks are managed through gRPC calls to the coordinator, including `GetSlave()`, `GetFollowSyncsForUsers()`, `HandleHeartBeats()`, `GetServer()`, and `CheckUserStatus()`.

### Follower Synchronizer program design

The synchronizer maintains consistency of the user and timeline files across all clusters without directly interacting with the servers in the clusters. It can only interact with the coordinator and other synchronizers to maintain consistency.

Main components of the Follower Synchronizer program:

* Monitor the master server's directory for changes to files, such as all users, relation, or timeline files.
* Determine the type of file that was altered, the new content since the last recorded change, and who must be informed of these changes.
* Make a call to the coordinator's `GetFollowSyncsForUsers()` to identify the other synchronizers to send the changes to.
* Use RPCs (`SyncUsers()`, `SyncRelations()`, `SyncTimeline()`) to inform other synchronizers of any changes that need to be made to files, allowing the receiving synchronizers to make those changes within their own cluster.

The synchronizer, on startup, uses threads to check for changes to the files in the its cluster:

* One thread for the users file.
* One thread for all relation files and timeline files.
