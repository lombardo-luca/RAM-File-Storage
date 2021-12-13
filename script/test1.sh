#!/bin/bash
./client -t 200 -f mysock -w testFiles/1 -W testFiles/1/file1 -D Wdir -r testFiles/1/file1 -R n=3 -d Rdir -p

exit 0

./client -t 200 -f mysock -w test -W test/filepesante -D Wdir -r test/filepesante -R n=3 -d Rdir -l test/filepesante -u test/filepesante -c test/filepesante -p

exit 0