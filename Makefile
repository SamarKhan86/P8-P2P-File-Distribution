CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -pthread -Iinclude

.PHONY: all clean test

all: metainfo tracker peer

metainfo: src/metainfo.c src/sha256.c src/protocol.c src/tracker.c
	$(CC) $(CFLAGS) -DMETAINFO_CLI -o $@ $^

tracker: src/tracker.c src/protocol.c src/sha256.c
	$(CC) $(CFLAGS) -DTRACKER_MAIN -o $@ $^

peer: src/peer.c src/peer_connections.c src/peer_transfer.c src/piece_manager.c src/rarest_first.c src/logger.c src/protocol.c src/metainfo.c src/sha256.c src/tracker.c
	$(CC) $(CFLAGS) -o $@ $^

test: all
	bash tests/test_multi_peer.sh

clean:
	rm -f metainfo tracker peer
