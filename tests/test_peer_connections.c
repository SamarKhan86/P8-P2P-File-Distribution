#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../src/peer.h"

int main(void)
{
    PeerManager manager;

    printf("Testing peer connections...\n\n");

    /* Test 1: Initialize manager */
    assert(peer_manager_init(&manager) == 0);
    assert(manager.peer_count == 0);
    printf("PASS: Initialize peer manager\n");

    /* Test 2: Add a peer */
    struct Peer peer;
    memset(&peer, 0, sizeof(peer));
    snprintf(peer.peer_id, sizeof(peer.peer_id), "P1");
    snprintf(peer.ip, sizeof(peer.ip), "127.0.0.1");
    peer.port = 6034;

    assert(peer_manager_add(&manager, &peer) == 0);
    assert(manager.peer_count == 1);
    assert(strcmp(manager.connections[0].peer.peer_id, "P1") == 0);
    printf("PASS: Add peer\n");

    /* Test 3: Reject invalid arguments */
    assert(peer_manager_init(NULL) == -1);
    assert(peer_manager_add(NULL, &peer) == -1);
    assert(peer_manager_add(&manager, NULL) == -1);
    printf("PASS: Reject NULL arguments\n");

    /* Test 4: Enforce peer limit */
    peer_manager_init(&manager);

    for (int i = 0; i < MAX_PEERS; i++) {
        struct Peer p;
        memset(&p, 0, sizeof(p));
        snprintf(p.peer_id, sizeof(p.peer_id), "P%d", i);
        snprintf(p.ip, sizeof(p.ip), "127.0.0.1");
        p.port = 6034 + i;

        assert(peer_manager_add(&manager, &p) == i);
    }

    assert(manager.peer_count == MAX_PEERS);

    struct Peer extra = {0};
    snprintf(extra.peer_id, sizeof(extra.peer_id), "EXTRA");
    assert(peer_manager_add(&manager, &extra) == -1);
    printf("PASS: Maximum peer limit\n");

    /* Test 5: Destroy manager */
    peer_manager_destroy(&manager);
    assert(manager.peer_count == 0);
    printf("PASS: Destroy peer manager\n");

    printf("\nALL BASIC PEER CONNECTION TESTS PASSED\n");
    return 0;
}

