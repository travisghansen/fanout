SRC = fanout.c
OBJ = ${SRC:.c=.o}
CFLAGS = -std=c99 -Wall -g
DESTDIR = /

fanout:

install: fanout
	install -Dm755 fanout $(DESTDIR)/usr/bin/fanout

clean:
	rm -f fanout
