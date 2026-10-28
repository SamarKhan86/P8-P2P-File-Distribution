#include "metainfo.h"
#include "sha256.h"
#include "tracker.h"

uint32_t metainfo_piece_len(const metainfo_t *meta, uint32_t piece_idx)
{
    if (!meta || piece_idx >= meta->piece_count)
        return 0;

    if (piece_idx == meta->piece_count - 1) {
        uint64_t remainder = meta->file_size % meta->piece_size;
        return remainder ? (uint32_t)remainder : meta->piece_size;
    }
    return meta->piece_size;
}

int metainfo_create(const char *input_path, const char *meta_path, uint32_t piece_size)
{
    if (!input_path || !meta_path || piece_size == 0) {
        fprintf(stderr, "[metainfo] Invalid arguments to metainfo_create\n");
        return -1;
    }

    FILE *fin = fopen(input_path, "rb");
    if (!fin) {
        perror("[metainfo] fopen input_path");
        return -1;
    }

    struct stat st;
    if (fstat(fileno(fin), &st) < 0) {
        perror("[metainfo] fstat");
        fclose(fin);
        return -1;
    }

    uint64_t file_size = (uint64_t)st.st_size;
    uint32_t piece_count = (uint32_t)((file_size + piece_size - 1) / piece_size);
    if (piece_count == 0) piece_count = 1;

    uint8_t *hashes = malloc(piece_count * SHA256_HASH_SIZE);
    uint8_t *buf = malloc(piece_size);
    if (!hashes || !buf) {
        fprintf(stderr, "[metainfo] Memory allocation failed\n");
        free(hashes);
        free(buf);
        fclose(fin);
        return -1;
    }

    for (uint32_t i = 0; i < piece_count; i++) {
        size_t n = fread(buf, 1, piece_size, fin);
        sha256_buffer(buf, n, hashes + (i * SHA256_HASH_SIZE));
    }
    fclose(fin);
    free(buf);

    /* Compute file_id as SHA-256 over all concatenated piece hashes */
    uint8_t file_id[SHA256_HASH_SIZE];
    sha256_buffer(hashes, (size_t)piece_count * SHA256_HASH_SIZE, file_id);

    FILE *fout = fopen(meta_path, "w");
    if (!fout) {
        perror("[metainfo] fopen meta_path");
        free(hashes);
        return -1;
    }

    /* Extract basename for filename */
    const char *base = strrchr(input_path, '/');
#ifdef _WIN32
    if (!base) base = strrchr(input_path, '\\');
#endif
    base = base ? base + 1 : input_path;

    char file_id_hex[65];
    hash_to_hex(file_id, file_id_hex);

    fprintf(fout, "NAME %s\n", base);
    fprintf(fout, "SIZE %llu\n", (unsigned long long)file_size);
    fprintf(fout, "PIECE_SIZE %u\n", piece_size);
    fprintf(fout, "PIECES %u\n", piece_count);
    fprintf(fout, "FILE_ID %s\n", file_id_hex);
    fprintf(fout, "HASHES\n");

    for (uint32_t i = 0; i < piece_count; i++) {
        char hash_hex[65];
        hash_to_hex(hashes + (i * SHA256_HASH_SIZE), hash_hex);
        fprintf(fout, "%s\n", hash_hex);
    }

    fclose(fout);
    free(hashes);
    return 0;
}

int metainfo_load(const char *meta_path, metainfo_t *meta)
{
    if (!meta_path || !meta) return -1;
    memset(meta, 0, sizeof(*meta));

    FILE *f = fopen(meta_path, "r");
    if (!f) {
        perror("[metainfo] fopen meta_path");
        return -1;
    }

    char line[512];
    int reading_hashes = 0;
    uint32_t hash_index = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (strlen(line) == 0) continue;

        if (!reading_hashes) {
            if (strncmp(line, "NAME ", 5) == 0) {
                size_t flen = strlen(line + 5);
                if (flen >= sizeof(meta->filename)) flen = sizeof(meta->filename) - 1;
                memcpy(meta->filename, line + 5, flen);
                meta->filename[flen] = '\0';
            } else if (strncmp(line, "SIZE ", 5) == 0) {
                meta->file_size = (uint64_t)strtoull(line + 5, NULL, 10);
            } else if (strncmp(line, "PIECE_SIZE ", 11) == 0) {
                meta->piece_size = (uint32_t)strtoul(line + 11, NULL, 10);
            } else if (strncmp(line, "PIECES ", 7) == 0) {
                meta->piece_count = (uint32_t)strtoul(line + 7, NULL, 10);
            } else if (strncmp(line, "FILE_ID ", 8) == 0) {
                hex_to_hash(line + 8, meta->file_id);
            } else if (strcmp(line, "HASHES") == 0) {
                reading_hashes = 1;
                if (meta->piece_count == 0) {
                    fprintf(stderr, "[metainfo] Invalid piece count in metadata\n");
                    fclose(f);
                    return -1;
                }
                meta->piece_hashes = malloc((size_t)meta->piece_count * SHA256_HASH_SIZE);
                if (!meta->piece_hashes) {
                    fprintf(stderr, "[metainfo] Memory allocation failed for hashes\n");
                    fclose(f);
                    return -1;
                }
            }
        } else {
            if (hash_index < meta->piece_count) {
                if (hex_to_hash(line, meta->piece_hashes + (hash_index * SHA256_HASH_SIZE)) != 0) {
                    fprintf(stderr, "[metainfo] Failed to parse hex hash on line: %s\n", line);
                    fclose(f);
                    metainfo_free(meta);
                    return -1;
                }
                hash_index++;
            }
        }
    }

    fclose(f);

    if (hash_index != meta->piece_count) {
        fprintf(stderr, "[metainfo] Expected %u hashes but found %u\n", meta->piece_count, hash_index);
        metainfo_free(meta);
        return -1;
    }

    return 0;
}

void metainfo_free(metainfo_t *meta)
{
    if (meta && meta->piece_hashes) {
        free(meta->piece_hashes);
        meta->piece_hashes = NULL;
    }
}

void metainfo_print(const metainfo_t *meta)
{
    if (!meta) return;
    char hex[65];
    hash_to_hex(meta->file_id, hex);

    printf("================ Metainfo ================\n");
    printf("Filename   : %s\n", meta->filename);
    printf("File Size  : %llu bytes\n", (unsigned long long)meta->file_size);
    printf("Piece Size : %u bytes\n", meta->piece_size);
    printf("Piece Count: %u\n", meta->piece_count);
    printf("File ID    : %s\n", hex);
    printf("==========================================\n");
}

#ifdef METAINFO_CLI
int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s create <input_file> <output_meta> [piece_size_bytes]\n", argv[0]);
        fprintf(stderr, "  %s show <meta_file>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "create") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s create <input_file> <output_meta> [piece_size_bytes]\n", argv[0]);
            return 1;
        }
        uint32_t piece_size = DEFAULT_PIECE_SIZE;
        if (argc >= 5) {
            piece_size = (uint32_t)strtoul(argv[4], NULL, 10);
            if (piece_size == 0) piece_size = DEFAULT_PIECE_SIZE;
        }
        if (metainfo_create(argv[2], argv[3], piece_size) != 0) {
            fprintf(stderr, "Failed to create metadata file %s\n", argv[3]);
            return 1;
        }
        printf("Successfully created metainfo: %s\n", argv[3]);
        return 0;
    } else if (strcmp(argv[1], "show") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s show <meta_file>\n", argv[0]);
            return 1;
        }
        metainfo_t meta;
        if (metainfo_load(argv[2], &meta) != 0) {
            fprintf(stderr, "Failed to load metadata file %s\n", argv[2]);
            return 1;
        }
        metainfo_print(&meta);
        metainfo_free(&meta);
        return 0;
    } else {
        fprintf(stderr, "Unknown command '%s'\n", argv[1]);
        return 1;
    }
}
#endif
