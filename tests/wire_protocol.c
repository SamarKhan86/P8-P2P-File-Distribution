#include "wire_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* Placeholder shared info hash for the current demo protocol. */
const char INFO_HASH[20] = "12345678901234567890";

int send_all(int socket_fd, const char *data, int size)
{
    int total = 0;

    while (total < size) {
        int n = (int)send(socket_fd, data + total, (size_t)(size - total), MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 0;
        total += n;
    }
    return 1;
}

int recv_all(int socket_fd, char *data, int size)
{
    int total = 0;

    while (total < size) {
        int n = (int)recv(socket_fd, data + total, (size_t)(size - total), 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 0;
        total += n;
    }
    return 1;
}

int send_handshake(int socket_fd, const char *peer_id)
{
    char msg[WP_HANDSHAKE_SIZE] = {0};

    if (peer_id == NULL)
        return 0;

    msg[0] = 19;
    memcpy(msg + 1, "BitTorrent protocol", 19);
    /* Bytes 20..27 are reserved and remain zero. */
    memcpy(msg + 28, INFO_HASH, 20);
    /* Copy at most 20 bytes; the zero-filled message pads shorter IDs. */
    size_t id_len = strlen(peer_id);
    if (id_len > 20)
        id_len = 20;
    memcpy(msg + 48, peer_id, id_len);

    return send_all(socket_fd, msg, WP_HANDSHAKE_SIZE);
}

int recv_handshake(int socket_fd)
{
    char msg[WP_HANDSHAKE_SIZE];

    if (!recv_all(socket_fd, msg, WP_HANDSHAKE_SIZE))
        return 0;

    if ((unsigned char)msg[0] != 19 ||
        memcmp(msg + 1, "BitTorrent protocol", 19) != 0 ||
        memcmp(msg + 28, INFO_HASH, 20) != 0) {
        fprintf(stderr, "Invalid handshake\n");
        return 0;
    }

    printf("Handshake successful\n");
    return 1;
}

int send_message(int socket_fd, int id, const char *data, uint32_t size)
{
    uint32_t net_length;
    unsigned char message_id;

    if (id < 0 || id > 255 || size > WP_MAX_SIZE - 1 || (size > 0 && data == NULL))
        return 0;

    net_length = htonl(size + 1);
    message_id = (unsigned char)id;

    if (!send_all(socket_fd, (const char *)&net_length, 4) ||
        !send_all(socket_fd, (const char *)&message_id, 1))
        return 0;

    return size == 0 || send_all(socket_fd, data, (int)size);
}

int send_keep_alive(int socket_fd)
{
    uint32_t zero = 0;
    return send_all(socket_fd, (const char *)&zero, 4);
}

int send_choke(int socket_fd) { return send_message(socket_fd, WP_CHOKE, NULL, 0); }
int send_unchoke(int socket_fd) { return send_message(socket_fd, WP_UNCHOKE, NULL, 0); }
int send_interested(int socket_fd) { return send_message(socket_fd, WP_INTERESTED, NULL, 0); }
int send_not_interested(int socket_fd) { return send_message(socket_fd, WP_NOT_INTERESTED, NULL, 0); }

int send_bitfield(int socket_fd, const char *bits, uint32_t size)
{
    if (size > 0 && bits == NULL)
        return 0;
    return send_message(socket_fd, WP_BITFIELD, bits, size);
}

int send_have(int socket_fd, uint32_t index)
{
    uint32_t value = htonl(index);
    return send_message(socket_fd, WP_HAVE, (const char *)&value, sizeof(value));
}

static int send_block_message(int socket_fd, int id,
                              uint32_t index, uint32_t begin, uint32_t length)
{
    uint32_t data[3];
    data[0] = htonl(index);
    data[1] = htonl(begin);
    data[2] = htonl(length);
    return send_message(socket_fd, id, (const char *)data, sizeof(data));
}

int send_request(int socket_fd, uint32_t index, uint32_t begin, uint32_t length)
{
    return send_block_message(socket_fd, WP_REQUEST, index, begin, length);
}

int send_cancel(int socket_fd, uint32_t index, uint32_t begin, uint32_t length)
{
    return send_block_message(socket_fd, WP_CANCEL, index, begin, length);
}

int send_piece(int socket_fd, uint32_t index, uint32_t begin,
               const char *block, uint32_t size)
{
    char header[8];
    char *data;
    uint32_t value;
    int result;

    if (size > WP_MAX_SIZE - 9 || (size > 0 && block == NULL))
        return 0;

    data = (char *)malloc((size_t)size + 8);
    if (data == NULL)
        return 0;

    value = htonl(index);
    memcpy(header, &value, 4);
    value = htonl(begin);
    memcpy(header + 4, &value, 4);
    memcpy(data, header, 8);
    if (size > 0)
        memcpy(data + 8, block, size);

    result = send_message(socket_fd, WP_PIECE, data, size + 8);
    free(data);
    return result;
}

int recv_message(int socket_fd, int *id, char *data, uint32_t *size)
{
    uint32_t net_length, length;
    unsigned char message_id;

    if (id == NULL || size == NULL || data == NULL)
        return 0;

    if (!recv_all(socket_fd, (char *)&net_length, 4))
        return 0;

    length = ntohl(net_length);
    if (length == 0) {
        *id = -1;
        *size = 0;
        return 1;
    }

    if (length > WP_MAX_SIZE)
        return 0;

    if (!recv_all(socket_fd, (char *)&message_id, 1))
        return 0;

    *id = message_id;
    *size = length - 1;

    if (*size > 0 && !recv_all(socket_fd, data, (int)*size))
        return 0;

    return 1;
}

int send_metadata(int socket_fd, uint64_t file_size,
                  uint32_t piece_size)
{
    if (piece_size == 0)
        return 0;

    uint64_t count = file_size / piece_size;
    if (file_size % piece_size != 0)
        count++;

    if (count > UINT32_MAX)
        return 0;

    uint32_t payload[4];

    payload[0] = htonl((uint32_t)(file_size >> 32));
    payload[1] = htonl((uint32_t)file_size);
    payload[2] = htonl(piece_size);
    payload[3] = htonl((uint32_t)count);

    return send_message(socket_fd, WP_METADATA,
                        (const char *)payload, sizeof(payload));
}

int recv_metadata(int socket_fd, uint64_t *file_size,
                  uint32_t *piece_size, uint32_t *piece_count)
{
    if (file_size == NULL || piece_size == NULL ||
        piece_count == NULL)
        return 0;

    char buffer[WP_MAX_SIZE];
    int id;
    uint32_t size;

    if (!recv_message(socket_fd, &id, buffer, &size))
        return 0;

    if (id != WP_METADATA || size != 16)
        return 0;

    uint32_t high, low, piece, count;

    memcpy(&high, buffer, 4);
    memcpy(&low, buffer + 4, 4);
    memcpy(&piece, buffer + 8, 4);
    memcpy(&count, buffer + 12, 4);

    high = ntohl(high);
    low = ntohl(low);
    piece = ntohl(piece);
    count = ntohl(count);

    if (piece == 0)
        return 0;

    uint64_t expected_count =
        ((uint64_t)high << 32 | low) / piece;

    if (((uint64_t)high << 32 | low) % piece != 0)
        expected_count++;

    if (expected_count != count)
        return 0;

    *file_size = ((uint64_t)high << 32) | low;
    *piece_size = piece;
    *piece_count = count;

    return 1;
}

