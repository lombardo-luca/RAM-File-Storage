#!/bin/bash

./client -t 200 -f mysock -w testFiles/1 -W testFiles/1/file1 -D Wdir -r testFiles/1/file1 -R n=3 -d Rdir -l testFiles/1/file1 -u testFiles/1/file1e -c testFiles/1/file1 -p &
./client -t 200 -f mysock -w testFiles/2 -W testFiles/2/file6 -D Wdir -r testFiles/2/file6 -R n=3 -d Rdir -l testFiles/2/file6 -u testFiles/2/file6 -c testFiles/2/file6 -p &
./client -t 200 -f mysock -w testFiles/3 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir -l testFiles/3/file11 -u testFiles/3/file11 -c testFiles/3/file11 -p &
./client -t 200 -f mysock -w testFiles/4 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir -l testFiles/3/file11 -u testFiles/3/file11 -c testFiles/3/file11 -p &
./client -t 200 -f mysock -w testFiles/5 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir -l testFiles/3/file11 -u testFiles/3/file11 -c testFiles/3/file11 -p 

exit 0