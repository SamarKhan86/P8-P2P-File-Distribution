#include "peer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Send all bytes, since TCP send() may write only part of a buffer. */
static int send_all(int fd, const char *buffer, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, buffer + sent, length - sent, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* Receive one line from the tracker, including neither CR nor LF. */
static int receive_line(int fd, char *buffer, size_t capacity)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        char ch;
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        if (ch == '\n')
            break;
        if (ch != '\r')
            buffer[used++] = ch;
    }
    buffer[used] = '\0';
    return 0;
}

/* Initialize peer manager. */
int peer_manager_init(PeerManager *manager)
{
    if (manager == NULL)
        return -1;

    manager->peer_count = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
        manager->connections[i].socket_fd = -1;
        manager->connections[i].connected = 0;
        memset(&manager->connections[i].peer, 0,
               sizeof(manager->connections[i].peer));
    }
    return 0;
}

/* Add a peer unless it is already present or the manager is full. */
int peer_manager_add(PeerManager *manager, const struct Peer *peer)
{
    if (manager == NULL || peer == NULL ||
        peer->port < 1 || peer->port > 65535)
        return -1;

    for (int i = 0; i < manager->peer_count; i++) {
        if (strcmp(manager->connections[i].peer.peer_id, peer->peer_id) == 0) {
            manager->connections[i].peer = *peer;
            return i;
        }
    }

    if (manager->peer_count >= MAX_PEERS)
        return -1;

    int index = manager->peer_count++;
    manager->connections[index].peer = *peer;
    manager->connections[index].socket_fd = -1;
    manager->connections[index].connected = 0;
    return index;
}

/*
 * Query the tracker API added to tracker.c.
 * Tracker protocol:
 *   client sends: GET\n
 *   server replies: "<peer_id> <ip> <port>\n" for each peer, then "END\n".
 *
 * Returns the number of newly added/known peers, or -1 on tracker failure.
 */
int peer_manager_discover_from_tracker(PeerManager *manager,
                                       const char *tracker_ip,
                                       int tracker_port)
{
    if (manager == NULL || tracker_ip == NULL ||
        tracker_port < 1 || tracker_port > 65535)
        return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("tracker socket");
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)tracker_port);

    if (inet_pton(AF_INET, tracker_ip, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid tracker IPv4 address: %s\n", tracker_ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect to tracker");
        close(fd);
        return -1;
    }

    if (send_all(fd, "GET\n", 4) != 0) {
        perror("send tracker GET");
        close(fd);
        return -1;
    }

    int discovered = 0;
    char line[128];
    while (receive_line(fd, line, sizeof(line)) == 0) {
        if (strcmp(line, "END") == 0)
            break;

        struct Peer peer;
        memset(&peer, 0, sizeof(peer));
        if (sscanf(line, "%19s %49s %d",
                   peer.peer_id, peer.ip, &peer.port) != 3 ||
            peer.port < 1 || peer.port > 65535) {
            fprintf(stderr, "Ignoring invalid tracker entry: %s\n", line);
            continue;
        }

        if (peer_manager_add(manager, &peer) >= 0)
            discovered++;
        else
            fprintf(stderr, "Peer list full; ignoring %s\n", peer.peer_id);
    }

    close(fd);
    return discovered;
}

/* Connect to one peer, preserving failure state for the caller. */
int peer_connect(PeerConnection *connection)
{
    if (connection == NULL)
        return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("peer socket");
        connection->connected = 0;
        connection->socket_fd = -1;
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)connection->peer.port);

    if (inet_pton(AF_INET, connection->peer.ip, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid peer IP for %s: %s\n",
                connection->peer.peer_id, connection->peer.ip);
        close(fd);
        connection->connected = 0;
        connection->socket_fd = -1;
        return -1;
    }

    printf("Connecting to %s (%s:%d)...\n",
           connection->peer.peer_id,
           connection->peer.ip,
           connection->peer.port);

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "Connection to peer %s failed: %s\n",
                connection->peer.peer_id, strerror(errno));
        close(fd);
        connection->connected = 0;
        connection->socket_fd = -1;
        return -1;
    }

    connection->socket_fd = fd;
    connection->connected = 1;
    printf("Connected to %s\n", connection->peer.peer_id);
    return 0;
}

/* Thread wrapper: records success/failure on the connection entry. */
void *connection_worker(void *arg)
{
    PeerConnection *connection = (PeerConnection *)arg;
    if (connection != NULL)
        (void)peer_connect(connection);
    return NULL;
}

/*
 * Connect to all registered peers concurrently.
 * Returns the number of successful connections, or -1 for invalid input /
 * thread creation failure. Failed peers are reported and left disconnected.
 */
int connect_to_all_peers(PeerManager *manager, int thread_count)
{
    if (manager == NULL || thread_count <= 0)
        return -1;

    pthread_t threads[MAX_PEERS];
    int started[MAX_PEERS] = {0};
    int count = manager->peer_count;
    if (count > thread_count)
        count = thread_count;
    if (count > MAX_PEERS)
        count = MAX_PEERS;

    for (int i = 0; i < count; i++) {
        int rc = pthread_create(&threads[i], NULL,
                                connection_worker,
                                &manager->connections[i]);
        if (rc != 0) {
            fprintf(stderr, "Could not start connection thread for %s: %s\n",
                    manager->connections[i].peer.peer_id, strerror(rc));
            continue;
        }
        started[i] = 1;
    }

    int successful = 0;
    for (int i = 0; i < count; i++) {
        if (started[i])
            pthread_join(threads[i], NULL);
        if (manager->connections[i].connected)
            successful++;
    }

    printf("Peer connections: %d successful out of %d attempted.\n",
           successful, count);
    return successful;
}

/* Close every open peer connection. */
void peer_manager_destroy(PeerManager *manager)
{
    if (manager == NULL)
        return;

    for (int i = 0; i < manager->peer_count; i++) {
        if (manager->connections[i].socket_fd >= 0) {
            close(manager->connections[i].socket_fd);
            manager->connections[i].socket_fd = -1;
        }
        manager->connections[i].connected = 0;
    }
    manager->peer_count = 0;
}

