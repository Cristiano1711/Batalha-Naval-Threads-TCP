CC     = gcc
CFLAGS = -Wall -Wextra -I battleship

BATTLESHIP_SRC = battleship/battleship.c battleship/battleship.h common/protocol.h

all: battleserver battleclient

battleserver: server/battleserver.c $(BATTLESHIP_SRC)
	$(CC) $(CFLAGS) -o server/battleserver server/battleserver.c battleship/battleship.c -lpthread

battleclient: client/battleclient.c $(BATTLESHIP_SRC)
	$(CC) $(CFLAGS) -o client/battleclient client/battleclient.c

clean:
	rm -f server/battleserver client/battleclient

.PHONY: all clean
