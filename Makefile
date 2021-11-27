CC			=  gcc
AR          =  ar
CFLAGS	    += -Wall -pedantic -g
ARFLAGS     =  rvs
INCDIR      = ./includes -I ./threadpool
INCLUDES	= -I. -I $(INCDIR)
LDFLAGS 	= -L.
OPTFLAGS	= #-O3 
LIBS        = -pthread

# aggiungere qui altri targets
TARGETS		= server client

.PHONY: all clean cleanall
.SUFFIXES: .c .h

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -c -o $@ $<

all		: $(TARGETS)

server: server.o libPool.a
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

libPool.a: ./includes/threadpool.o ./includes/threadpool.h
	$(AR) $(ARFLAGS) $@ $<

server.o: server.c

client: client.o
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

client.o: client.c

./includes/threadpool.o: ./includes/threadpool.c

clean		: 
	rm -f $(TARGETS)
cleanall	: clean
	\rm -f *.o *~ *.a ./mysock