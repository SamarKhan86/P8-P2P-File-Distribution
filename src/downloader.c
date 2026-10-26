#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "wire_protocol.h"
#include "piece_manager.h"

#define PIECE_SIZE 1024

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        printf("Usage: %s <server_ip> <port> <output_file>\n",
               argv[0]);
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server = {0};

    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[2]));

    if (inet_pton(AF_INET, argv[1],
                  &server.sin_addr) != 1)
    {
        printf("Invalid server IP\n");
        close(sockfd);
        return 1;
    }

    if (connect(sockfd, (struct sockaddr *)&server,
                sizeof(server)) < 0)
    {
        perror("connect");
        close(sockfd);
        return 1;
    }

    if (!send_handshake(sockfd, "DOWNLOADER") ||
        !recv_handshake(sockfd))
    {
        printf("Handshake failed\n");
        close(sockfd);
        return 1;
    }

    uint64_t file_size;
    uint32_t piece_size;
    uint32_t piece_count;

    if (!recv_metadata(sockfd, &file_size,
                       &piece_size, &piece_count))
    {
        printf("Could not receive file metadata\n");
        close(sockfd);
        return 1;
    }

    if (piece_size == 0 || piece_size > WP_MAX_SIZE - 9 ||
        piece_size > PIECE_SIZE ||
        piece_count > 1000000)
    {
        printf("Unsupported file metadata\n");
        close(sockfd);
        return 1;
    }

    FILE *output = fopen(argv[3], "wb");

    if (output == NULL)
    {
        perror("output file");
        close(sockfd);
        return 1;
    }

    PieceManager manager;

    if (piece_manager_init(&manager, (int)piece_count) != 0)
    {
        printf("Could not initialize piece manager\n");
        fclose(output);
        close(sockfd);
        return 1;
    }

    if (!send_interested(sockfd))
    {
        printf("Could not send interested message\n");
        goto failure;
    }

    /* Wait until the seeder unchokes us. */
    while (1)
    {
        char message[WP_MAX_SIZE];
        int id;
        uint32_t size;

        if (!recv_message(sockfd, &id, message, &size))
            goto failure;

        if (id == WP_UNCHOKE)
            break;

        if (id == WP_CHOKE)
        {
            printf("Seeder choked the downloader\n");
            goto failure;
        }
    }

    for (uint32_t i = 0; i < piece_count; i++)
    {
        uint32_t length = piece_size;

        uint64_t offset = (uint64_t)i * piece_size;

        if (offset + length > file_size)
            length = (uint32_t)(file_size - offset);

        if (!send_request(sockfd, i, 0, length))
            goto failure;

        char message[WP_MAX_SIZE];
        int id;
        uint32_t size;

        while (1)
        {
            if (!recv_message(sockfd, &id, message, &size))
                goto failure;

            if (id != WP_PIECE)
                continue;

            if (size < 8)
                goto failure;

            uint32_t received_index;
            uint32_t received_begin;

            memcpy(&received_index, message, 4);
            memcpy(&received_begin, message + 4, 4);

            received_index = ntohl(received_index);
            received_begin = ntohl(received_begin);

            if (received_index != i || received_begin != 0)
                continue;

            if (size - 8 != length)
                goto failure;

            if (fwrite(message + 8, 1, length, output) != length)
                goto failure;

            break;
        }

        mark_piece(&manager, (int)i);

        printf("Downloaded piece %u/%u\n", i + 1, piece_count);
    }

    if (fflush(output) != 0)
        goto failure;

    piece_manager_destroy(&manager);
    fclose(output);
    close(sockfd);

    printf("Download complete: %s\n", argv[3]);
    return 0;

failure:
    printf("Download failed\n");

    piece_manager_destroy(&manager);
    fclose(output);
    close(sockfd);

    return 1;
}

