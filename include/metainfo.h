#ifndef METAINFO_H
#define METAINFO_H

#include "common.h"

typedef struct {
    char filename[256];
    uint64_t file_size;
    uint32_t piece_size;
    uint32_t piece_count;
    uint8_t file_id[SHA256_HASH_SIZE];
    uint8_t *piece_hashes; /* piece_count * SHA256_HASH_SIZE */
} metainfo_t;

/* Creates a .meta file for a given input file */
int metainfo_create(const char *input_path, const char *meta_path, uint32_t piece_size);

/* Loads a .meta file into memory */
int metainfo_load(const char *meta_path, metainfo_t *meta);

/* Frees dynamically allocated memory in metainfo_t */
void metainfo_free(metainfo_t *meta);

/* Prints metadata summary to stdout */
void metainfo_print(const metainfo_t *meta);

/* Calculates the exact byte length of a given piece (accounting for the last piece) */
uint32_t metainfo_piece_len(const metainfo_t *meta, uint32_t piece_idx);

#endif /* METAINFO_H */
