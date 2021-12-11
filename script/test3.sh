#!/bin/bash

for ((i = 0; i <= 30; i++)); do
	./client -t 0 -f mysock -w testFiles/1 -W testFiles/1/file1 -D Wdir -r testFiles/1/file1 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/2 -W testFiles/2/file6 -D Wdir -r testFiles/2/file6 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/3 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/4 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/5 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/1 -W testFiles/1/file1 -D Wdir -r testFiles/1/file1 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/2 -W testFiles/2/file6 -D Wdir -r testFiles/2/file6 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/3 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/4 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/4 -W testFiles/3/file11 -D Wdir -r testFiles/3/file11 -R n=3 -d Rdir &
	./client -t 0 -f mysock -w testFiles/6 -W testFiles/6/f5 -D Wdir -r testFiles/6/f5 -R n=3 -d Rdir &
	sleep 1
done

exit 0