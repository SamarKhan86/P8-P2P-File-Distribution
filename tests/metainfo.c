#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"

#define PIECE_SIZE 1024

void generate_metainfo(const char *filename)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL)
    {
        printf("Error: Cannot open file.\n");
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);

    int number_of_pieces =
        (file_size + PIECE_SIZE - 1) / PIECE_SIZE;

    char meta_filename[256];

    snprintf(meta_filename,
             sizeof(meta_filename),
             "%s.meta",
             filename);

    FILE *meta = fopen(meta_filename, "w");

    if (meta == NULL)
    {
        printf("Error: Cannot create metainfo file.\n");
        fclose(file);
        return;
    }

    fprintf(meta, "Filename: %s\n", filename);
    fprintf(meta, "File Size: %ld bytes\n", file_size);
    fprintf(meta, "Piece Size: %d bytes\n", PIECE_SIZE);
    fprintf(meta, "Number of Pieces: %d\n", number_of_pieces);

    unsigned char buffer[PIECE_SIZE];
    unsigned char hash[SHA256_DIGEST_LENGTH];

    for (int i = 0; i < number_of_pieces; i++)
    {
        size_t bytes_read =
            fread(buffer, 1, PIECE_SIZE, file);

        SHA256_CTX ctx;

        sha256_init(&ctx);
        sha256_update(&ctx, buffer, bytes_read);
        sha256_final(&ctx, hash);

        fprintf(meta, "Piece %d Hash: ", i);

        for (int j = 0; j < SHA256_DIGEST_LENGTH; j++)
        {
            fprintf(meta, "%02x", hash[j]);
        }

        fprintf(meta, "\n");
    }

    fclose(file);
    fclose(meta);

    printf("Metainfo generated successfully: %s\n",
           meta_filename);
}

int main()
{
    char filename[256];

    printf("Enter filename: ");
    scanf("%255s", filename);

    generate_metainfo(filename);

    return 0;
}