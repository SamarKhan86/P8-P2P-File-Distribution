#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// Initialize peer manager
int peer_manager_init(PeerManager *manager)
{
if (manager == NULL)
return -1;


manager->peer_count = 0;

for (int i = 0; i < MAX_PEERS; i++)
{
    manager->connections[i].socket_fd = -1;
    manager->connections[i].connected = 0;
}

return 0;


}

// Add a peer
int peer_manager_add(PeerManager *manager, const struct Peer *peer)
{
if (manager == NULL || peer == NULL)
return -1;

if (manager->peer_count >= MAX_PEERS)
    return -1;

int index = manager->peer_count;

manager->connections[index].peer = *peer;
manager->connections[index].socket_fd = -1;
manager->connections[index].connected = 0;

manager->peer_count++;

return index;


}

// Connect to one peer
int peer_connect(PeerConnection *connection)
{
int fd = socket(AF_INET, SOCK_STREAM, 0);


if (fd < 0)
{
    perror("Socket creation failed");
    return -1;
}

struct sockaddr_in address;
memset(&address, 0, sizeof(address));

address.sin_family = AF_INET;
address.sin_port = htons(connection->peer.port);
inet_pton(AF_INET, connection->peer.ip, &address.sin_addr);

printf("Connecting to %s...\n", connection->peer.peer_id);

if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
{
    perror("Connection failed");
    close(fd);
    return -1;
}

connection->socket_fd = fd;
connection->connected = 1;

printf("Connected to %s\n", connection->peer.peer_id);

return 0;


}

// Function executed by each thread
void *connection_worker(void *arg)
{
PeerConnection *connection = arg;


peer_connect(connection);

return NULL;


}

// Connect to all peers concurrently
int connect_to_all_peers(PeerManager *manager, int thread_count)
{
if (manager == NULL || thread_count <= 0)
return -1;


pthread_t threads[MAX_PEERS];
int count = manager->peer_count;

if (count > thread_count)
    count = thread_count;

for (int i = 0; i < count; i++)
{
    pthread_create(
        &threads[i],
        NULL,
        connection_worker,
        &manager->connections[i]
    );
}

for (int i = 0; i < count; i++)
{
    pthread_join(threads[i], NULL);
}

return 0;


}

// Close all connections
void peer_manager_destroy(PeerManager *manager)
{
for (int i = 0; i < manager->peer_count; i++)
{
if (manager->connections[i].socket_fd >= 0)
{
close(manager->connections[i].socket_fd);
manager->connections[i].socket_fd = -1;
}

    manager->connections[i].connected = 0;
}

manager->peer_count = 0;

}
