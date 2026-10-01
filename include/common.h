#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

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

/* Global configuration constants */
#define DEFAULT_PIECE_SIZE    16384     /* 16 KB default piece size */
#define MAX_PEERS             32        /* Maximum connected peers in a swarm */
#define PIPELINE_DEPTH        4         /* Number of in-flight piece requests per peer */
#define SHA256_HASH_SIZE      32        /* SHA-256 digest length in bytes */
#define PEER_ID_LEN           20        /* Identifier length for each peer */
#define FILE_ID_LEN           32        /* File / swarm identifier (SHA-256) */
#define HANDSHAKE_MAGIC       "P2PLITE1"/* Protocol magic string (8 bytes) */
#define HANDSHAKE_LEN         60        /* 8 bytes magic + 32 bytes file_id + 20 bytes peer_id */

/* Helper macros for bit manipulation */
#define BIT_SET(arr, i)   ((arr)[(i) / 8] |=  (1 << (7 - ((i) % 8))))
#define BIT_CLEAR(arr, i) ((arr)[(i) / 8] &= ~(1 << (7 - ((i) % 8))))
#define BIT_CHECK(arr, i) (((arr)[(i) / 8] &   (1 << (7 - ((i) % 8)))) != 0)

#endif /* COMMON_H */
