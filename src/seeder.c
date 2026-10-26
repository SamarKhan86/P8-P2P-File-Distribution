#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "wire_protocol.h"

#define PIECE_SIZE 1024

void serve_peer(int client_fd, const char *filename)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL)
    {
        perror("Cannot open file");
        close(client_fd);
        return;
    }

    if (!recv_handshake(client_fd) ||
        !send_handshake(client_fd, "SEEDER"))
    {
        printf("Handshake failed\n");
        goto cleanup;
    }

    if (fseek(file, 0, SEEK_END) != 0)
        goto cleanup;

    long file_size = ftell(file);

    if (file_size < 0)
        goto cleanup;

    if (!send_metadata(client_fd, (uint64_t)file_size, PIECE_SIZE))
        goto cleanup;

    printf("Peer connected. Serving %s\n", filename);

    char buffer[WP_MAX_SIZE];
    char block[PIECE_SIZE];

    while (1)
    {
        int id;
        uint32_t size;

        if (!recv_message(client_fd, &id, buffer, &size))
            break;

        if (id == WP_INTERESTED)
        {
            send_unchoke(client_fd);
        }
        else if (id == WP_REQUEST && size == 12)
        {
            uint32_t request[3];

            memcpy(request, buffer, 12);

            uint32_t index = ntohl(request[0]);
            uint32_t begin = ntohl(request[1]);
            uint32_t length = ntohl(request[2]);

            if (length == 0 || length > PIECE_SIZE ||
                begin >= PIECE_SIZE)
                continue;

            uint64_t offset =
                (uint64_t)index * PIECE_SIZE + begin;

            if (offset + length > (uint64_t)file_size)
                continue;

            if (fseek(file, (long)offset, SEEK_SET) != 0)
                break;

            size_t bytes_read = fread(block, 1, length, file);

            if (bytes_read != length)
                break;

            if (!send_piece(client_fd, index, begin,
                            block, (uint32_t)bytes_read))
                break;

            printf("Sent piece %u\n", index);
        }
    }

cleanup:
    fclose(file);
    close(client_fd);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s <filename> <port>\n", argv[0]);
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int option = 1;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               &option, sizeof(option));

    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(atoi(argv[2]));

    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Seeder listening on port %s\n", argv[2]);

    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        serve_peer(client_fd, argv[1]);
    }

    close(server_fd);
    return 0;
}

