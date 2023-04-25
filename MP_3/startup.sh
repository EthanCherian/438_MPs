#!/bin/bash

commands=(
"mkdir logs"
"GLOG_log_dir=./logs ./coordinator -p 9000"
"GLOG_log_dir=./logs ./synchronizer --cip localhost --cp 9000 -p 9070 --id 1"
"GLOG_log_dir=./logs ./synchronizer --cip localhost --cp 9000 -p 9080 --id 2"
"GLOG_log_dir=./logs ./synchronizer --cip localhost --cp 9000 -p 9090 --id 3"
"GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10001 --id 1 -t master"
"GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10002 --id 2 -t master"
"GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10003 --id 3 -t master"
"GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10004 --id 1 -t slave"
"GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10005 --id 2 -t slave"
"GLOG_log_dir=./logs ./tsd --cip localhost --cp 9000 -p 10006 --id 3 -t slave"
)

client_startup=(
"GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 1"
"GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 2"
"GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 3"
"GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 4"
"GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 5"
"GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 6"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 7"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 8"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 9"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 10"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 11"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 12"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 13"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 14"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 15"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 16"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 17"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 18"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 19"
# "GLOG_log_dir=./logs ./tsc --cip localhost --cp 9000 -p 10007 --id 20"
)

# run each command in a separate terminal
# for cmd in "${client_startup[@]}"; do
# # for cmd in "${commands[@]}"; do
#   gnome-terminal -- bash -c "$cmd; exec bash"
# done

for cmd in "${commands[@]}"; do
  gnome-terminal -- bash -c "$cmd; exec bash"
done