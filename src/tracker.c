#include "tracker.h"
#include "protocol.h"
#include "sha256.h"

void hash_to_hex(const uint8_t hash[SHA256_HASH_SIZE], char hex[65])
{
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        hex[i * 2]     = hex_chars[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hex_chars[hash[i] & 0x0F];
    }
    hex[64] = '\0';
}

int hex_to_hash(const char *hex, uint8_t hash[SHA256_HASH_SIZE])
{
    if (!hex || strlen(hex) < 64) return -1;
    for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        unsigned int val;
        if (sscanf(hex + (i * 2), "%2x", &val) != 1) return -1;
        hash[i] = (uint8_t)val;
    }
    return 0;
}

int tracker_announce(const char *tracker_ip, uint16_t tracker_port,
                     const uint8_t file_id[SHA256_HASH_SIZE], uint16_t my_port,
                     peer_endpoint_t peers[], int max_peers, int *peer_count)
{
    if (peer_count) *peer_count = 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[tracker_client] socket");
        return -1;
    }

    /* Set short 3-second timeout so peer doesn't hang if tracker is unreachable */
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(tracker_port);
    if (inet_pton(AF_INET, tracker_ip, &saddr.sin_addr) <= 0) {
        fprintf(stderr, "[tracker_client] Invalid tracker IP: %s\n", tracker_ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        close(fd);
        return -1;
    }

    char file_id_hex[65];
    hash_to_hex(file_id, file_id_hex);

    char req[128];
    int req_len = snprintf(req, sizeof(req), "ANNOUNCE %s %u\n", file_id_hex, my_port);
    if (send_all(fd, req, (size_t)req_len) != 0) {
        close(fd);
        return -1;
    }

    /* Read response lines: "PEERS <count>\n" then rows of "<ip> <port>\n" */
    char resp[4096];
    int total_bytes = 0;
    while (total_bytes < (int)sizeof(resp) - 1) {
        ssize_t n = recv(fd, resp + total_bytes, sizeof(resp) - 1 - total_bytes, 0);
        if (n <= 0) break;
        total_bytes += (int)n;
    }
    close(fd);
    resp[total_bytes] = '\0';

    char *line = strtok(resp, "\r\n");
    if (!line || strncmp(line, "PEERS ", 6) != 0) {
        return -1;
    }

    int expected_peers = atoi(line + 6);
    int count = 0;

    while ((line = strtok(NULL, "\r\n")) != NULL && count < max_peers && count < expected_peers) {
        char p_ip[64];
        int p_port;
        if (sscanf(line, "%63s %d", p_ip, &p_port) == 2) {
            snprintf(peers[count].ip, sizeof(peers[count].ip), "%s", p_ip);
            peers[count].port = (uint16_t)p_port;
            count++;
        }
    }

    if (peer_count) *peer_count = count;
    return 0;
}

#ifdef TRACKER_MAIN

#define MAX_TRACKER_ENTRIES 256

typedef struct {
    char file_id_hex[65];
    char ip[64];
    uint16_t port;
    time_t last_seen;
} tracker_entry_t;

static tracker_entry_t g_registry[MAX_TRACKER_ENTRIES];
static int g_registry_count = 0;
static pthread_mutex_t g_tracker_mutex = PTHREAD_MUTEX_INITIALIZER;

static void register_peer(const char *file_id_hex, const char *ip, uint16_t port)
{
    pthread_mutex_lock(&g_tracker_mutex);
    time_t now = time(NULL);

    /* Update existing entry */
    for (int i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i].file_id_hex, file_id_hex) == 0 &&
            strcmp(g_registry[i].ip, ip) == 0 &&
            g_registry[i].port == port) {
            g_registry[i].last_seen = now;
            pthread_mutex_unlock(&g_tracker_mutex);
            return;
        }
    }

    /* Add new entry */
    if (g_registry_count < MAX_TRACKER_ENTRIES) {
        snprintf(g_registry[g_registry_count].file_id_hex, sizeof(g_registry[g_registry_count].file_id_hex), "%s", file_id_hex);
        snprintf(g_registry[g_registry_count].ip, sizeof(g_registry[g_registry_count].ip), "%s", ip);
        g_registry[g_registry_count].port = port;
        g_registry[g_registry_count].last_seen = now;
        g_registry_count++;
        printf("[tracker] Registered new peer: %s:%u for file %s\n", ip, port, file_id_hex);
    }
    pthread_mutex_unlock(&g_tracker_mutex);
}

static void handle_client(int client_fd, const char *client_ip)
{
    char buf[512];
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buf[n] = '\0';

    char cmd[32], file_id[65];
    int port;
    if (sscanf(buf, "%31s %64s %d", cmd, file_id, &port) == 3 && strcmp(cmd, "ANNOUNCE") == 0) {
        register_peer(file_id, client_ip, (uint16_t)port);

        /* Generate response with all peers in this swarm */
        pthread_mutex_lock(&g_tracker_mutex);
        int match_count = 0;
        for (int i = 0; i < g_registry_count; i++) {
            if (strcmp(g_registry[i].file_id_hex, file_id) == 0) {
                match_count++;
            }
        }

        char resp[4096];
        int offset = snprintf(resp, sizeof(resp), "PEERS %d\n", match_count);
        for (int i = 0; i < g_registry_count && offset < (int)sizeof(resp) - 64; i++) {
            if (strcmp(g_registry[i].file_id_hex, file_id) == 0) {
                offset += snprintf(resp + offset, sizeof(resp) - offset,
                                  "%s %u\n", g_registry[i].ip, g_registry[i].port);
            }
        }
        pthread_mutex_unlock(&g_tracker_mutex);

        send_all(client_fd, resp, (size_t)offset);
    } else {
        const char *err = "ERR Invalid request\n";
        send_all(client_fd, err, strlen(err));
    }

    close(client_fd);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "[tracker] Invalid port: %s\n", argv[1]);
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[tracker] socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("[tracker] bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 64) < 0) {
        perror("[tracker] listen");
        close(server_fd);
        return 1;
    }

    printf("==========================================\n");
    printf(" Tracker running on port %d\n", port);
    printf("==========================================\n");

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int client_fd = accept(server_fd, (struct sockaddr *)&caddr, &clen);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("[tracker] accept");
            break;
        }

        char client_ip[64];
        inet_ntop(AF_INET, &caddr.sin_addr, client_ip, sizeof(client_ip));
        handle_client(client_fd, client_ip);
    }

    close(server_fd);
    return 0;
}
#endif
