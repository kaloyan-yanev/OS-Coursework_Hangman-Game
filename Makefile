CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
COMMON  = game.c net-serv.c
HEADERS = game.h net-serv.h

all: hangman-server hangman-client

hangman-server: server.c $(COMMON) $(HEADERS)
	$(CC) $(CFLAGS) -o hangman-server server.c $(COMMON)

hangman-client: client.c $(COMMON) $(HEADERS)
	$(CC) $(CFLAGS) -o hangman-client client.c $(COMMON)

clean:
	rm -f hangman-server hangman-client

.PHONY: all clean
