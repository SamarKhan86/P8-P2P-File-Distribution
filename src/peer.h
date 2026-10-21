#ifndef PEER_H
#define PEER_H

#define MAX_PEERS 10
#define PEER_ID_SIZE 20
#define PEER_IP_SIZE 50

#include <pthread.h>

struct Peer {
    char peer_id[PEER_ID_SIZE];
    char ip[PEER_IP_SIZE];
    int port;
};

typedef struct {
    struct Peer peer;
    int socket_fd;
    int connected;
} PeerConnection;

typedef struct {
    PeerConnection connections[MAX_PEERS];
    int peer_count;
} PeerManager;

typedef enum {
    PEER_DISCONNECTED,
    PEER_CONNECTING,
    PEER_CONNECTED,
    PEER_DOWNLOADING,
    PEER_FAILED
} PeerState;

typedef struct {
    char peer_id[PEER_ID_SIZE];
    PeerState state;
} PeerStateEntry;

typedef struct {
    PeerStateEntry peers[MAX_PEERS];
    int peer_count;
} PeerStateManager;

int peer_manager_init(PeerManager *manager);
int peer_manager_add(PeerManager *manager, const struct Peer *peer);
int peer_connect(PeerConnection *connection);
void *connection_worker(void *arg);
int connect_to_all_peers(PeerManager *manager, int thread_count);
void peer_manager_destroy(PeerManager *manager);

void init_peer_states(PeerStateManager *manager);
int add_peer_state(PeerStateManager *manager, const char *peer_id);
int update_peer_state(PeerStateManager *manager,
                      const char *peer_id, PeerState new_state);
const char *get_state_name(PeerState state);
void display_peer_states(const PeerStateManager *manager);

#endif
