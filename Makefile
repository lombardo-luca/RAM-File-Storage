SHELL := /bin/bash

CC			=  gcc
AR          =  ar
CFLAGS	    += -Wall -Wno-pointer-arith -pedantic -g
ARFLAGS     =  rvs
INCDIR      = ./includes -I ./threadpool -I ./fileQueue -I ./partialIO -I ./api
INCLUDES	= -I. -I $(INCDIR)
LDFLAGS 	= -L.
OPTFLAGS	= #-O3 
LIBS        = -pthread

# aggiungere qui altri targets
TARGETS		= server client

.PHONY: all clean cleanall test1
.SUFFIXES: .c .h

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -c -o $@ $<

all		: $(TARGETS)

server: server.o libPool.a libQueue.a libIO.a
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

client: client.o libAPI.a libIO.a
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

libPool.a: ./includes/threadpool.o ./includes/threadpool.h
	$(AR) $(ARFLAGS) $@ $<

libQueue.a: ./includes/fileQueue.o ./includes/fileQueue.h
	$(AR) $(ARFLAGS) $@ $<

libIO.a: ./includes/partialIO.o ./includes/partialIO.h
	$(AR) $(ARFLAGS) $@ $<

libAPI.a: ./includes/api.o ./includes/api.h 
	$(AR) $(ARFLAGS) $@ $<

server.o: server.c

client.o: client.c

./includes/threadpool.o: ./includes/threadpool.c

./includes/fileQueue.o: ./includes/fileQueue.c

./includes/partialIO.o: ./includes/partialIO.c

./includes/api.o: ./includes/api.c

clean		: 
	rm -f $(TARGETS)
cleanall	: clean
	\rm -f *.o *~ *.a ./mysock

test1	:
	printf "threadpoolSize:1\nsockName:mysock\nmaxFiles:10000\nmaxSize:128000\nlogFile:logs" > config/config.txt
	valgrind --leak-check=full ./server & last_pid=$$!; ./script/test1.sh; kill -1 $$last_pid
