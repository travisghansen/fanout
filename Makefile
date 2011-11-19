OBJS = 
CC = gcc
DEBUG = -g
CFLAGS = -Wall -c $(DEBUG)
LFLAGS = -Wall $(DEBUG)

.PHONY : $(OBJS)

fanout :
	$(CC) $(LFLAGS) $(OBJS) -o fanout fanout.c

clean:
	\rm  fanout

