CC = gcc
CFLAGS = -Wall -pthread

all: s-talk

s-talk:
	$(CC) $(CFLAGS) simple-talk.c list.o list.h -o s-talk -pthread

clean:
	rm -f s-talk

