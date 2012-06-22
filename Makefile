PROJ = fanout
OBJS = 
CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG)

.PHONY : $(OBJS)

fanout : fanout.c
	$(CC) $(LFLAGS) $(OBJS) -o $(PROJ) fanout.c

clean:
	\rm  fanout

