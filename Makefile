PROJ = fanout
OBJS = 
CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG)

.PHONY : $(OBJS)

fanout : fanout.c
	$(CC) $(LFLAGS) $(OBJS) -o $(PROJ) fanout.c

debian-install:
	-groupadd -f fanout
	-useradd -g fanout -d /var/run/fanout -m -s /bin/false fanout
	cp fanout /usr/local/bin
	cp debian/fanout.init /etc/init.d/fanout
	cp debian/fanout.default /etc/default/fanout

clean:
	\rm  fanout

