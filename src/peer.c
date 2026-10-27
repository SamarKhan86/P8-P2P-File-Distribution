#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include "peer.h"
#include "protocol.h"
#include "tracker.h"
#include "sha256.h"
#include "metainfo.h"
#include "rarest_first.h"
#include "peer_transfer.h"
#include "peer_connections.h"
#include "peer_runtime.h"
#include "piece_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

peer_state_t g_state;
static char g_tracker_ip[64] = "";
static uint16_t g_tracker_port = 0;
static int g_has_tracker = 0;
static int g_is_seeder = 0;
static int g_exit_on_complete = 0;
static int g_seed_time = -1; /* -1: default; seeders run forever, downloaders run or seed */
static const char *g_log_file = NULL;
static double g_start_time = 0.0;
int g_listen_fd = -1;

static double get_time_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void handle_sigint(int sig)
{
    (void)sig;
    g_state.running = 0;
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
}

static void schedule_requests(void)
{
    pthread_mutex_lock(&g_state.state_mutex);
    if (g_state.verified_count >= g_state.meta.piece_count) {
        pthread_mutex_unlock(&g_state.state_mutex);
        return;
    }

    for (int p = 0; p < MAX_PEERS; p++) {
        peer_conn_t *peer = &g_state.peers[p];
        if (!peer->is_active || peer->sock_fd < 0) continue;

        while (peer->in_flight < PIPELINE_DEPTH) {
            int best_piece = choose_rarest_piece(&g_state, peer);

            if (best_piece < 0) break; /* No pieces currently available on this peer */

            g_state.piece_status[best_piece] = PIECE_IN_FLIGHT;
            g_state.piece_assigned_peer[best_piece] = p;
            peer->in_flight++;

            pthread_mutex_unlock(&g_state.state_mutex);
            peer_send_request(peer, (uint32_t)best_piece);
            logger_event(&g_state.logger, g_state.my_port, "piece_requested", peer->ip,
                         peer->port, best_piece, metainfo_piece_len(&g_state.meta, (uint32_t)best_piece), "sent", "rarest-first selection");
            pthread_mutex_lock(&g_state.state_mutex);
        }
    }
    pthread_mutex_unlock(&g_state.state_mutex);
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <meta_file> <data_file> <my_port> [tracker_ip tracker_port | tracker_ip:port] [options]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --seed               Explicitly run as initial seeder\n");
    fprintf(stderr, "  --peer <ip:port>     Directly connect to peer (can be repeated)\n");
    fprintf(stderr, "  --tracker <ip:port>  Specify tracker\n");
    fprintf(stderr, "  --exit-on-complete   Exit immediately when download completes\n");
    fprintf(stderr, "  --seed-time <sec>    Seed for <sec> seconds after download completes before exiting\n");
    fprintf(stderr, "  --log <file>         Append CSV performance/transfer log\n");
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char *meta_file = argv[1];
    const char *data_file = argv[2];
    int my_port = atoi(argv[3]);
    if (my_port <= 0 || my_port > 65535) {
        fprintf(stderr, "[peer] Invalid listening port: %s\n", argv[3]);
        return 1;
    }

    char direct_peers[MAX_PEERS][64];
    int direct_peer_count = 0;

    int arg_idx = 4;
    while (arg_idx < argc) {
        if (strcmp(argv[arg_idx], "--seed") == 0) {
            g_is_seeder = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--exit-on-complete") == 0) {
            g_exit_on_complete = 1;
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--seed-time") == 0 && arg_idx + 1 < argc) {
            g_seed_time = atoi(argv[++arg_idx]);
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--log") == 0 && arg_idx + 1 < argc) {
            g_log_file = argv[++arg_idx];
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--peer") == 0 && arg_idx + 1 < argc) {
            if (direct_peer_count < MAX_PEERS) {
                snprintf(direct_peers[direct_peer_count++], sizeof(direct_peers[0]), "%s", argv[++arg_idx]);
            }
            arg_idx++;
        } else if (strcmp(argv[arg_idx], "--tracker") == 0 && arg_idx + 1 < argc) {
            char t_arg[64];
            snprintf(t_arg, sizeof(t_arg), "%s", argv[++arg_idx]);
            char *colon = strchr(t_arg, ':');
            if (colon) {
                *colon = '\0';
                snprintf(g_tracker_ip, sizeof(g_tracker_ip), "%s", t_arg);
                g_tracker_port = (uint16_t)atoi(colon + 1);
                g_has_tracker = 1;
            }
            arg_idx++;
        } else if (argv[arg_idx][0] != '-') {
            /* Could be tracker IP and port */
            if (strchr(argv[arg_idx], ':')) {
                char t_arg[64];
                snprintf(t_arg, sizeof(t_arg), "%s", argv[arg_idx]);
                char *colon = strchr(t_arg, ':');
                *colon = '\0';
                snprintf(g_tracker_ip, sizeof(g_tracker_ip), "%s", t_arg);
                g_tracker_port = (uint16_t)atoi(colon + 1);
                g_has_tracker = 1;
                arg_idx++;
            } else if (arg_idx + 1 < argc && argv[arg_idx + 1][0] != '-') {
                snprintf(g_tracker_ip, sizeof(g_tracker_ip), "%s", argv[arg_idx]);
                g_tracker_port = (uint16_t)atoi(argv[arg_idx + 1]);
                g_has_tracker = 1;
                arg_idx += 2;
            } else {
                arg_idx++;
            }
        } else {
            arg_idx++;
        }
    }

    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    /* Load metadata */
    if (metainfo_load(meta_file, &g_state.meta) != 0) {
        fprintf(stderr, "[peer] Failed to load metadata from %s\n", meta_file);
        return 1;
    }

    g_state.my_port = (uint16_t)my_port;
    snprintf(g_state.peer_id, sizeof(g_state.peer_id), "PEER_%05u_%08lx",
             my_port, (unsigned long)time(NULL));
    g_state.running = 1;
    pthread_mutex_init(&g_state.state_mutex, NULL);

    g_state.bitfield_bytes = (g_state.meta.piece_count + 7) / 8;
    g_state.my_bitfield = calloc(g_state.bitfield_bytes, 1);
    g_state.piece_status = calloc(g_state.meta.piece_count, sizeof(piece_status_t));
    g_state.piece_assigned_peer = malloc(g_state.meta.piece_count * sizeof(int));

    for (uint32_t i = 0; i < g_state.meta.piece_count; i++) {
        g_state.piece_assigned_peer[i] = -1;
    }

    for (int i = 0; i < MAX_PEERS; i++) {
        g_state.peers[i].sock_fd = -1;
        g_state.peers[i].is_active = 0;
        g_state.peers[i].bitfield = calloc(g_state.bitfield_bytes, 1);
        pthread_mutex_init(&g_state.peers[i].send_mutex, NULL);
    }

    /* Open target file */
    g_state.file_fd = open(data_file, O_RDWR | O_CREAT, 0644);
    if (g_state.file_fd < 0) {
        perror("[peer] Failed to open/create data file");
        return 1;
    }

    /* Recognize verified pieces if this peer is resuming a prior download. */
    piece_manager_scan_existing(&g_state);

    if (g_state.verified_count == g_state.meta.piece_count) {
        g_is_seeder = 1;
    } else {
        /* Ensure file is sized appropriately */
        if (ftruncate(g_state.file_fd, (off_t)g_state.meta.file_size) < 0) {
            perror("[peer] ftruncate");
        }
    }

    printf("==========================================\n");
    printf(" Peer ID       : %s\n", g_state.peer_id);
    printf(" Port          : %u\n", g_state.my_port);
    printf(" File          : %s (%llu bytes)\n", data_file, (unsigned long long)g_state.meta.file_size);
    printf(" Pieces        : %u (Piece size: %u bytes)\n", g_state.meta.piece_count, g_state.meta.piece_size);
    printf(" Role          : %s (%u/%u verified)\n",
           g_is_seeder ? "SEEDER" : "DOWNLOADER", g_state.verified_count, g_state.meta.piece_count);
    if (g_has_tracker) {
        printf(" Tracker       : %s:%u\n", g_tracker_ip, g_tracker_port);
    }
    printf("==========================================\n");

    g_start_time = get_time_s();
    if (g_log_file && logger_open(&g_state.logger, g_log_file, g_start_time) != 0)
        fprintf(stderr, "[peer] Could not open CSV log: %s\n", g_log_file);

    /* Start TCP listener */
    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        perror("[peer] socket");
        return 1;
    }

    int opt = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(g_state.my_port);

    if (bind(g_listen_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("[peer] bind");
        close(g_listen_fd);
        return 1;
    }

    if (listen(g_listen_fd, 32) < 0) {
        perror("[peer] listen");
        close(g_listen_fd);
        return 1;
    }

    pthread_t l_thread;
    pthread_create(&l_thread, NULL, peer_listener_thread, NULL);

    /* Connect to explicit direct peers */
    for (int i = 0; i < direct_peer_count; i++) {
        char *colon = strchr(direct_peers[i], ':');
        if (colon) {
            *colon = '\0';
            uint16_t pport = (uint16_t)atoi(colon + 1);
            peer_connect_to_remote_peer(direct_peers[i], pport);
        }
    }

    double last_tracker_announce = 0.0;
    double complete_time = 0.0;
    int announced_complete = 0;

    while (g_state.running) {
        double now = get_time_s();

        /* Announce to tracker every 3 seconds */
        if (g_has_tracker && (now - last_tracker_announce > 3.0)) {
            last_tracker_announce = now;
            peer_endpoint_t disc_peers[MAX_PEERS];
            int disc_count = 0;
            if (tracker_announce(g_tracker_ip, g_tracker_port, g_state.meta.file_id,
                                 g_state.my_port, disc_peers, MAX_PEERS, &disc_count) == 0) {
                for (int i = 0; i < disc_count; i++) {
                    peer_connect_to_remote_peer(disc_peers[i].ip, disc_peers[i].port);
                }
            }
        }

        /* If downloading, schedule requests */
        if (g_state.verified_count < g_state.meta.piece_count) {
            schedule_requests();
        } else if (!announced_complete) {
            announced_complete = 1;
            complete_time = now;
            double elapsed = complete_time - g_start_time;

            int active_cnt = 0;
            pthread_mutex_lock(&g_state.state_mutex);
            for (int i = 0; i < MAX_PEERS; i++) {
                if (g_state.peers[i].is_active) active_cnt++;
            }
            pthread_mutex_unlock(&g_state.state_mutex);

            printf("\n[peer %u] ==========================================\n", g_state.my_port);
            printf("[peer %u] Download completed successfully!\n", g_state.my_port);
            printf("[peer %u] Pieces verified : %u / %u (100%%)\n", g_state.my_port, g_state.verified_count, g_state.meta.piece_count);
            printf("[peer %u] Elapsed time    : %.3f seconds\n", g_state.my_port, elapsed);
            printf("[peer %u] Swarm peers     : %d\n", g_state.my_port, active_cnt);
            printf("[peer %u] ==========================================\n\n", g_state.my_port);

            char details[96];
            snprintf(details, sizeof(details), "verified_pieces=%u active_peers=%d", g_state.verified_count, active_cnt);
            logger_event(&g_state.logger, g_state.my_port, "download_complete", "", 0,
                         -1, 0, "complete", details);

            if (g_exit_on_complete) {
                break;
            }
        }

        /* Check seed-time timeout for completed downloaders */
        if (announced_complete && !g_is_seeder) {
            if (g_seed_time >= 0 && (now - complete_time >= g_seed_time)) {
                printf("[peer %u] Seeding window (%d s) finished. Exiting.\n", g_state.my_port, g_seed_time);
                break;
            } else if (g_seed_time < 0 && (now - complete_time >= 5.0)) {
                /* Default 5s seeding for completed downloader so peers can fetch pieces */
                printf("[peer %u] Default seeding period finished. Exiting.\n", g_state.my_port);
                break;
            }
        }

        /* Check seed-time for seeder if specified */
        if (g_is_seeder && g_seed_time > 0 && (now - g_start_time >= g_seed_time)) {
            printf("[peer %u] Seeder completed allotted run time (%d s). Exiting.\n", g_state.my_port, g_seed_time);
            break;
        }

        usleep(15000); /* 15 ms tick */
    }

    printf("[peer %u] Shutting down...\n", g_state.my_port);

    pthread_mutex_lock(&g_state.state_mutex);
    g_state.running = 0;
    for (int i = 0; i < MAX_PEERS; i++) {
        if (g_state.peers[i].sock_fd >= 0) {
            close(g_state.peers[i].sock_fd);
            g_state.peers[i].sock_fd = -1;
            g_state.peers[i].is_active = 0;
        }
        if (g_state.peers[i].bitfield) {
            free(g_state.peers[i].bitfield);
            g_state.peers[i].bitfield = NULL;
        }
    }
    pthread_mutex_unlock(&g_state.state_mutex);

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    if (g_state.file_fd >= 0) {
        close(g_state.file_fd);
        g_state.file_fd = -1;
    }

    logger_close(&g_state.logger);

    free(g_state.my_bitfield);
    free(g_state.piece_status);
    free(g_state.piece_assigned_peer);
    metainfo_free(&g_state.meta);

    return 0;
}
