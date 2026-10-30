#include "protocol.h"

int send_all(int fd, const void *buf, size_t len)
{
    const char *ptr = (const char *)buf;
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t sent = send(fd, ptr + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) return -1; /* Socket closed */
        total_sent += (size_t)sent;
    }
    return 0;
}

int recv_all(int fd, void *buf, size_t len)
{
    char *ptr = (char *)buf;
    size_t total_recv = 0;

    while (total_recv < len) {
        ssize_t n = recv(fd, ptr + total_recv, len - total_recv, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1; /* Peer disconnected */
        total_recv += (size_t)n;
    }
    return 0;
}

int send_handshake(int fd, const uint8_t file_id[SHA256_HASH_SIZE], const char peer_id[PEER_ID_LEN])
{
    char buf[HANDSHAKE_LEN];
    memcpy(buf, HANDSHAKE_MAGIC, 8);
    memcpy(buf + 8, file_id, SHA256_HASH_SIZE);
    memset(buf + 8 + SHA256_HASH_SIZE, 0, PEER_ID_LEN);
    strncpy(buf + 8 + SHA256_HASH_SIZE, peer_id, PEER_ID_LEN);

    return send_all(fd, buf, HANDSHAKE_LEN);
}

int recv_handshake(int fd, uint8_t file_id[SHA256_HASH_SIZE], char peer_id[PEER_ID_LEN])
{
    char buf[HANDSHAKE_LEN];
    if (recv_all(fd, buf, HANDSHAKE_LEN) != 0)
        return -1;

    if (memcmp(buf, HANDSHAKE_MAGIC, 8) != 0) {
        fprintf(stderr, "[protocol] Handshake magic mismatch\n");
        return -1;
    }

    memcpy(file_id, buf + 8, SHA256_HASH_SIZE);
    memcpy(peer_id, buf + 8 + SHA256_HASH_SIZE, PEER_ID_LEN);
    return 0;
}

int send_msg(int fd, uint8_t msg_id, const void *payload, uint32_t payload_len)
{
    uint32_t total_len = 1 + payload_len;
    uint32_t net_len = htonl(total_len);

    /* Allocate buffer for header + payload to send in one syscall */
    size_t packet_size = 4 + 1 + payload_len;
    uint8_t *pkt = malloc(packet_size);
    if (!pkt) return -1;

    memcpy(pkt, &net_len, 4);
    pkt[4] = msg_id;
    if (payload && payload_len > 0) {
        memcpy(pkt + 5, payload, payload_len);
    }

    int ret = send_all(fd, pkt, packet_size);
    free(pkt);
    return ret;
}

int recv_msg(int fd, uint8_t *msg_id, void **payload, uint32_t *payload_len)
{
    uint32_t net_len;
    if (recv_all(fd, &net_len, 4) != 0)
        return -1;

    uint32_t total_len = ntohl(net_len);
    if (total_len < 1 || total_len > (64 * 1024 * 1024)) { /* Sanity limit: 64MB */
        fprintf(stderr, "[protocol] Invalid wire message length: %u\n", total_len);
        return -1;
    }

    if (recv_all(fd, msg_id, 1) != 0)
        return -1;

    uint32_t p_len = total_len - 1;
    *payload_len = p_len;

    if (p_len > 0) {
        *payload = malloc(p_len);
        if (!*payload) return -1;

        if (recv_all(fd, *payload, p_len) != 0) {
            free(*payload);
            *payload = NULL;
            return -1;
        }
    } else {
        *payload = NULL;
    }

    return 0;
}

int send_bitfield(int fd, const uint8_t *bits, uint32_t bitfield_bytes)
{
    return send_msg(fd, MSG_BITFIELD, bits, bitfield_bytes);
}

int send_have(int fd, uint32_t piece_idx)
{
    uint32_t net_idx = htonl(piece_idx);
    return send_msg(fd, MSG_HAVE, &net_idx, sizeof(net_idx));
}

int send_request(int fd, uint32_t piece_idx)
{
    uint32_t net_idx = htonl(piece_idx);
    return send_msg(fd, MSG_REQUEST, &net_idx, sizeof(net_idx));
}

int send_piece(int fd, uint32_t piece_idx, const uint8_t *data, uint32_t len)
{
    uint32_t net_idx = htonl(piece_idx);
    size_t payload_len = 4 + len;
    uint8_t *payload = malloc(payload_len);
    if (!payload) return -1;

    memcpy(payload, &net_idx, 4);
    memcpy(payload + 4, data, len);

    int ret = send_msg(fd, MSG_PIECE, payload, (uint32_t)payload_len);
    free(payload);
    return ret;
}
