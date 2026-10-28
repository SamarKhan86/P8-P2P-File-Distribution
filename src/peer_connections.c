#include "peer_connections.h"
#include "peer_transfer.h"
#include "peer_runtime.h"
#include "protocol.h"

void peer_disconnect_locked(int peer_idx)
{
    peer_conn_t *p = &g_state.peers[peer_idx];
    if (!p->is_active && p->sock_fd < 0) return;

    printf("[peer %u] Peer %s:%u disconnected\n", g_state.my_port, p->ip, p->port);
    logger_event(&g_state.logger, g_state.my_port, "peer_disconnected", p->ip, p->port,
                 -1, 0, "ok", "connection closed");
    if (p->sock_fd >= 0) {
        close(p->sock_fd);
        p->sock_fd = -1;
    }
    p->is_active = 0;
    p->in_flight = 0;

    /* Revert in-flight piece assignments */
    for (uint32_t i = 0; i < g_state.meta.piece_count; i++) {
        if (g_state.piece_assigned_peer[i] == peer_idx && g_state.piece_status[i] == PIECE_IN_FLIGHT) {
            g_state.piece_status[i] = PIECE_MISSING;
            g_state.piece_assigned_peer[i] = -1;
        }
    }
}

static int add_active_peer(int sock_fd, const char *peer_id, const char *ip, uint16_t port)
{
    pthread_mutex_lock(&g_state.state_mutex);

    /* Check if already connected */
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_state.peers[i].is_active &&
            strcmp(g_state.peers[i].ip, ip) == 0 &&
            g_state.peers[i].port == port) {
            pthread_mutex_unlock(&g_state.state_mutex);
            close(sock_fd);
            return -1;
        }
    }

    int slot = -1;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!g_state.peers[i].is_active && g_state.peers[i].sock_fd < 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&g_state.state_mutex);
        close(sock_fd);
        return -1;
    }

    peer_conn_t *peer = &g_state.peers[slot];
    peer->sock_fd = sock_fd;
    snprintf(peer->peer_id, sizeof(peer->peer_id), "%s", peer_id ? peer_id : "UNKNOWN");
    snprintf(peer->ip, sizeof(peer->ip), "%s", ip);
    peer->port = port;
    peer->in_flight = 0;
    peer->is_active = 1;
    pthread_mutex_init(&peer->send_mutex, NULL);
    memset(peer->bitfield, 0, g_state.bitfield_bytes);

    /* Send our bitfield immediately after connection */
    peer_send_bitfield(peer, g_state.my_bitfield, g_state.bitfield_bytes);

    pthread_create(&peer->thread, NULL, peer_worker, (void *)(intptr_t)slot);
    pthread_detach(peer->thread);

    printf("[peer %u] Connected to peer %s:%u (id: %s)\n", g_state.my_port, ip, port, peer->peer_id);
    logger_event(&g_state.logger, g_state.my_port, "peer_connected", ip, port,
                 -1, 0, "ok", "handshake and bitfield sent");
    pthread_mutex_unlock(&g_state.state_mutex);
    return slot;
}

void *peer_listener_thread(void *arg)
{
    (void)arg;
    while (g_state.running && g_listen_fd >= 0) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int client_fd = accept(g_listen_fd, (struct sockaddr *)&caddr, &clen);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        char client_ip[64];
        inet_ntop(AF_INET, &caddr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(caddr.sin_port);

        /* Handshake exchange */
        uint8_t remote_file_id[SHA256_HASH_SIZE];
        char remote_peer_id[PEER_ID_LEN + 1];
        memset(remote_peer_id, 0, sizeof(remote_peer_id));

        if (recv_handshake(client_fd, remote_file_id, remote_peer_id) != 0 ||
            memcmp(remote_file_id, g_state.meta.file_id, SHA256_HASH_SIZE) != 0) {
            close(client_fd);
            continue;
        }

        if (send_handshake(client_fd, g_state.meta.file_id, g_state.peer_id) != 0) {
            close(client_fd);
            continue;
        }

        if (add_active_peer(client_fd, remote_peer_id, client_ip, client_port) < 0) {
            /* Full or already connected */
        }
    }
    return NULL;
}

int peer_connect_to_remote_peer(const char *ip, uint16_t port)
{
    /* Don't connect to ourselves */
    if (port == g_state.my_port && (strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "localhost") == 0)) {
        return -1;
    }

    pthread_mutex_lock(&g_state.state_mutex);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_state.peers[i].is_active &&
            strcmp(g_state.peers[i].ip, ip) == 0 &&
            g_state.peers[i].port == port) {
            pthread_mutex_unlock(&g_state.state_mutex);
            return 0; /* Already connected */
        }
    }
    pthread_mutex_unlock(&g_state.state_mutex);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* Timeout for connect */
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &saddr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        close(fd);
        return -1;
    }

    /* Handshake exchange */
    if (send_handshake(fd, g_state.meta.file_id, g_state.peer_id) != 0) {
        close(fd);
        return -1;
    }

    uint8_t remote_file_id[SHA256_HASH_SIZE];
    char remote_peer_id[PEER_ID_LEN + 1];
    memset(remote_peer_id, 0, sizeof(remote_peer_id));

    if (recv_handshake(fd, remote_file_id, remote_peer_id) != 0 ||
        memcmp(remote_file_id, g_state.meta.file_id, SHA256_HASH_SIZE) != 0) {
        close(fd);
        return -1;
    }

    if (add_active_peer(fd, remote_peer_id, ip, port) < 0) {
        return -1;
    }

    return 0;
}

