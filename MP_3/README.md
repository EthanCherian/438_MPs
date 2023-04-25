./coordinator -p 9000
./tsd -cip localhost -cp 9000 -p 10000 -id 1 -t master
./tsd -cip localhost -cp 9000 -p 10001 -id 2 -t master
./tsd -cip localhost -cp 9000 -p 10002 -id 3 -t master
./tsd -cip localhost -cp 9000 -p 10003 -id 1 -t slave
./tsd -cip localhost -cp 9000 -p 10004 -id 2 -t slave
./tsd -cip localhost -cp 9000 -p 10005 -id 3 -t slave
./synchronizer -cip localhost -cp 9000 -p 9090 -id 1
./synchronizer -cip localhost -cp 9000 -p 9080 -id 2
./synchronizer -cip localhost -cp 9000 -p 9070 -id 3
