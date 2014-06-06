PROJ = fanout
OBJS = 
CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG)
DESTDIR = /

.PHONY : $(OBJS)

fanout : fanout.c
	$(CC) $(LFLAGS) $(OBJS) -o $(PROJ) fanout.c

install:
	mkdir -p $(DESTDIR)/usr/sbin
	cp fanout $(DESTDIR)/usr/sbin/fanout

clean:
	test \! -f fanout || rm fanout
