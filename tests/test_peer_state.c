#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/peer.h"

int main(void)
{
    PeerStateManager manager;

    printf("Testing peer state management...\n\n");

    /* Test 1: Initialize */
    init_peer_states(&manager);
    assert(manager.peer_count == 0);
    printf("PASS: Initialize manager\n");

    /* Test 2: Add peers */
    int p1 = add_peer_state(&manager, "P1");
    int p2 = add_peer_state(&manager, "P2");

    assert(p1 == 0);
    assert(p2 == 1);
    assert(manager.peer_count == 2);
    printf("PASS: Add peers\n");

    /* Test 3: Check initial states */
    assert(manager.peers[p1].state == PEER_DISCONNECTED);
    assert(manager.peers[p2].state == PEER_DISCONNECTED);
    printf("PASS: Initial state is DISCONNECTED\n");

    /* Test 4: Update states */
    assert(update_peer_state(&manager, "P1", PEER_CONNECTED) == 0);
    assert(update_peer_state(&manager, "P2", PEER_DOWNLOADING) == 0);

    assert(manager.peers[p1].state == PEER_CONNECTED);
    assert(manager.peers[p2].state == PEER_DOWNLOADING);
    printf("PASS: Update peer states\n");

    /* Test 5: Update nonexistent peer */
    assert(update_peer_state(&manager, "UNKNOWN", PEER_CONNECTED) == -1);
    printf("PASS: Reject unknown peer\n");

    /* Test 6: State names */
    assert(strcmp(get_state_name(PEER_DISCONNECTED), "DISCONNECTED") == 0);
    assert(strcmp(get_state_name(PEER_CONNECTED), "CONNECTED") == 0);
    assert(strcmp(get_state_name(PEER_DOWNLOADING), "DOWNLOADING") == 0);
    assert(strcmp(get_state_name(PEER_FAILED), "FAILED") == 0);
    printf("PASS: State names\n");

    /* Test 7: Fill the manager to capacity */
    init_peer_states(&manager);

    for (int i = 0; i < MAX_PEERS; i++) {
        char id[PEER_ID_SIZE];
        snprintf(id, sizeof(id), "Peer%d", i);
        assert(add_peer_state(&manager, id) == i);
    }

    assert(manager.peer_count == MAX_PEERS);
    assert(add_peer_state(&manager, "ExtraPeer") == -1);
    printf("PASS: Maximum peer limit\n");

    /* Test 8: NULL handling */
    init_peer_states(NULL);
    assert(add_peer_state(NULL, "P1") == -1);
    assert(add_peer_state(&manager, NULL) == -1);
    assert(update_peer_state(NULL, "P1", PEER_CONNECTED) == -1);
    assert(update_peer_state(&manager, NULL, PEER_CONNECTED) == -1);
    printf("PASS: NULL argument handling\n");

    /* Test 9: Display states */
    display_peer_states(&manager);
    printf("\nALL PEER STATE TESTS PASSED\n");

    return 0;
}

