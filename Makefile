TEAM = einskislidid
VERSION = 1
HANDINDIR = /labs/proxylab/handin

CC = gcc
CFLAGS = -Wall -g 
LDFLAGS = -lpthread

OBJS = proxy.o csapp.o

all: proxy

proxy: $(OBJS)

csapp.o: csapp.c
	$(CC) $(CFLAGS) -c csapp.c

proxy.o: proxy.c
	$(CC) $(CFLAGS) -c proxy.c

handin:
	cp proxy.c $(HANDINDIR)/$(TEAM)-$(VERSION)-proxy.c
	chmod 600 $(HANDINDIR)/$(TEAM)-$(VERSION)-proxy.c


clean:
	rm -f *~ *.o proxy core

