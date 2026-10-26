#include "peer.h"
#include <stdio.h>
#include <string.h>

// Initialize the state manager
void init_peer_states(PeerStateManager *manager)
{
    if (manager == NULL)
        return;

    manager->peer_count = 0;
}

// Add a new peer
int add_peer_state(PeerStateManager *manager, const char *peer_id)
{
    if (manager == NULL || peer_id == NULL || peer_id[0] == '\0')
        return -1;

    // Avoid adding the same peer more than once.
    for (int i = 0; i < manager->peer_count; i++)
    {
        if (strcmp(manager->peers[i].peer_id, peer_id) == 0)
            return i;
    }

    if (manager->peer_count >= MAX_PEERS)
        return -1;

    int index = manager->peer_count;

    snprintf(
        manager->peers[index].peer_id,
        sizeof(manager->peers[index].peer_id),
        "%s",
        peer_id
    );

    manager->peers[index].state = PEER_DISCONNECTED;
    manager->peer_count++;

    return index;
}

// Update the state of an existing peer
int update_peer_state(
    PeerStateManager *manager,
    const char *peer_id,
    PeerState new_state
)
{
    if (manager == NULL || peer_id == NULL || peer_id[0] == '\0')
        return -1;

    // Reject values that are not defined in peer.h.
    if (new_state < PEER_DISCONNECTED || new_state > PEER_FAILED)
        return -1;

    for (int i = 0; i < manager->peer_count; i++)
    {
        if (strcmp(manager->peers[i].peer_id, peer_id) == 0)
        {
            manager->peers[i].state = new_state;
            return 0;
        }
    }

    return -1;
}

// Convert a state into a readable string
const char *get_state_name(PeerState state)
{
    switch (state)
    {
        case PEER_DISCONNECTED:
            return "DISCONNECTED";

        case PEER_CONNECTING:
            return "CONNECTING";

        case PEER_CONNECTED:
            return "CONNECTED";

        case PEER_DOWNLOADING:
            return "DOWNLOADING";

        case PEER_FAILED:
            return "FAILED";

        default:
            return "UNKNOWN";
    }
}

// Display all peer states
void display_peer_states(const PeerStateManager *manager)
{
    if (manager == NULL)
        return;

    printf("\nPeer States\n");
    printf("-------------------------\n");

    for (int i = 0; i < manager->peer_count; i++)
    {
        printf(
            "%s : %s\n",
            manager->peers[i].peer_id,
            get_state_name(manager->peers[i].state)
        );
    }
}

