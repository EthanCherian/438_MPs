#!/bin/bash

# Kill all processes created by the script
pkill -f "mkdir logs"
pkill -f "GLOG_log_dir=./logs ./coordinator -p 9000"
pkill -f "GLOG_log_dir=./logs ./synchronizer --cip localhost --cp 9000 -p 9070 --id 1"
pkill -f "GLOG_log_dir=./logs ./synchronizer --cip localhost --cp 9000 -p 9080 --id 2"
pkill -f "GLOG_log_dir=./logs ./synchronizer --cip localhost --cp 9000 -p 9090 --id 3"
pkill -f "GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10001 --id 1 -t master"
pkill -f "GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10002 --id 2 -t master"
pkill -f "GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10003 --id 3 -t master"
pkill -f "GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10004 --id 1 -t slave"
pkill -f "GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10005 --id 2 -t slave"
pkill -f "GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10006 --id 3 -t slave"
