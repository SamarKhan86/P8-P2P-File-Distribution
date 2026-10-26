#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_PEERS 10
#define TRACKER_PORT 6000
#define BUFFER_SIZE 512

struct Peer
{
    char peer_id[20];
    char ip[50];
    int port;
};

static struct Peer peers[MAX_PEERS];
static int peer_count = 0;
static pthread_mutex_t peers_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Existing interactive registration menu, retained for manual testing. */
void register_peer(void)
{
    char id[20], ip[50];
    int port;

    printf("Enter Peer ID: ");
    if (scanf("%19s", id) != 1) return;
    printf("Enter IP Address: ");
    if (scanf("%49s", ip) != 1) return;
    printf("Enter Port: ");
    if (scanf("%d", &port) != 1 || port < 1 || port > 65535) {
        printf("Invalid port.\n");
        return;
    }

    pthread_mutex_lock(&peers_mutex);

    for (int i = 0; i < peer_count; i++) {
        if (strcmp(peers[i].peer_id, id) == 0) {
            snprintf(peers[i].ip, sizeof(peers[i].ip), "%s", ip);
            peers[i].port = port;
            pthread_mutex_unlock(&peers_mutex);
            printf("Peer updated successfully.\n");
            return;
        }
    }

    if (peer_count >= MAX_PEERS) {
        pthread_mutex_unlock(&peers_mutex);
        printf("Tracker is full.\n");
        return;
    }

    snprintf(peers[peer_count].peer_id, sizeof(peers[peer_count].peer_id), "%s", id);
    snprintf(peers[peer_count].ip, sizeof(peers[peer_count].ip), "%s", ip);
    peers[peer_count].port = port;
    peer_count++;

    pthread_mutex_unlock(&peers_mutex);
    printf("Peer registered successfully.\n");
}

void show_peers(void)
{
    pthread_mutex_lock(&peers_mutex);

    if (peer_count == 0) {
        printf("No peers available.\n");
        pthread_mutex_unlock(&peers_mutex);
        return;
    }

    printf("\nAvailable Peers:\n");
    for (int i = 0; i < peer_count; i++) {
        printf("----------------------\n");
        printf("Peer ID: %s\n", peers[i].peer_id);
        printf("IP Address: %s\n", peers[i].ip);
        printf("Port: %d\n", peers[i].port);
    }

    pthread_mutex_unlock(&peers_mutex);
}

void remove_peer(void)
{
    char id[20];
    int found = 0;

    printf("Enter Peer ID to remove: ");
    if (scanf("%19s", id) != 1) return;

    pthread_mutex_lock(&peers_mutex);
    for (int i = 0; i < peer_count; i++) {
        if (strcmp(peers[i].peer_id, id) == 0) {
            for (int j = i; j < peer_count - 1; j++)
                peers[j] = peers[j + 1];
            peer_count--;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&peers_mutex);

    printf(found ? "Peer removed successfully.\n" : "Peer not found.\n");
}

static int send_all(int fd, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/*
 * Small line-based TCP API:
 *   REGISTER <peer_id> <ip> <port>\n
 *   GET\n
 *   REMOVE <peer_id>\n
 *
 * Replies:
 *   OK REGISTERED\n / ERR <reason>\n
 *   For GET: one "peer_id ip port\n" row per peer, followed by "END\n".
 */
static void handle_client(int client_fd)
{
    char buffer[BUFFER_SIZE];
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) return;
    buffer[n] = '\0';

    /* Use only the first command line received on this connection. */
    char *newline = strpbrk(buffer, "\r\n");
    if (newline) *newline = '\0';

    char command[16] = {0};
    if (sscanf(buffer, "%15s", command) != 1) {
        send_all(client_fd, "ERR empty command\n", 18);
        return;
    }

    if (strcmp(command, "REGISTER") == 0) {
        char id[20], ip[50];
        int port;
        if (sscanf(buffer, "REGISTER %19s %49s %d", id, ip, &port) != 3 ||
            port < 1 || port > 65535) {
            send_all(client_fd, "ERR usage: REGISTER <peer_id> <ip> <port>\n", 42);
            return;
        }

        pthread_mutex_lock(&peers_mutex);
        int slot = -1;
        for (int i = 0; i < peer_count; i++) {
            if (strcmp(peers[i].peer_id, id) == 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0 && peer_count < MAX_PEERS)
            slot = peer_count++;
        if (slot >= 0) {
            snprintf(peers[slot].peer_id, sizeof(peers[slot].peer_id), "%s", id);
            snprintf(peers[slot].ip, sizeof(peers[slot].ip), "%s", ip);
            peers[slot].port = port;
        }
        pthread_mutex_unlock(&peers_mutex);

        if (slot < 0)
            send_all(client_fd, "ERR tracker full\n", 17);
        else
            send_all(client_fd, "OK REGISTERED\n", 14);
    } else if (strcmp(command, "GET") == 0) {
        char row[128];
        pthread_mutex_lock(&peers_mutex);
        for (int i = 0; i < peer_count; i++) {
            int len = snprintf(row, sizeof(row), "%s %s %d\n",
                               peers[i].peer_id, peers[i].ip, peers[i].port);
            if (len > 0 && send_all(client_fd, row, (size_t)len) != 0)
                break;
        }
        pthread_mutex_unlock(&peers_mutex);
        send_all(client_fd, "END\n", 4);
    } else if (strcmp(command, "REMOVE") == 0) {
        char id[20];
        if (sscanf(buffer, "REMOVE %19s", id) != 1) {
            send_all(client_fd, "ERR usage: REMOVE <peer_id>\n", 28);
            return;
        }

        int found = 0;
        pthread_mutex_lock(&peers_mutex);
        for (int i = 0; i < peer_count; i++) {
            if (strcmp(peers[i].peer_id, id) == 0) {
                for (int j = i; j < peer_count - 1; j++)
                    peers[j] = peers[j + 1];
                peer_count--;
                found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&peers_mutex);

        send_all(client_fd, found ? "OK REMOVED\n" : "ERR peer not found\n",
                 found ? 11 : 19);
    } else {
        send_all(client_fd, "ERR commands: REGISTER, GET, REMOVE\n", 36);
    }
}

static void *tracker_server(void *unused)
{
    (void)unused;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("tracker socket");
        return NULL;
    }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(TRACKER_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("tracker bind");
        close(server_fd);
        return NULL;
    }
    if (listen(server_fd, 16) < 0) {
        perror("tracker listen");
        close(server_fd);
        return NULL;
    }

    printf("Tracker TCP API listening on port %d\n", TRACKER_PORT);
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("tracker accept");
            continue;
        }
        handle_client(client_fd);
        close(client_fd);
    }
    return NULL;
}

int main(void)
{
    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, tracker_server, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }
    pthread_detach(server_thread);

    int choice;
    while (1) {
        printf("\n===== P2P TRACKER =====\n");
        printf("1. Register Peer (manual)\n");
        printf("2. Show Peers\n");
        printf("3. Remove Peer (manual)\n");
        printf("4. Exit\n");
        printf("Enter choice: ");

        if (scanf("%d", &choice) != 1)
            break;

        switch (choice) {
            case 1: register_peer(); break;
            case 2: show_peers(); break;
            case 3: remove_peer(); break;
            case 4: printf("Tracker stopped.\n"); return 0;
            default: printf("Invalid choice.\n");
        }
    }

    return 0;
}

