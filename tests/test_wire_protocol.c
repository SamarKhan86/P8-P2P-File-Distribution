#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../src/wire_protocol.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        printf("FAIL: %s (line %d)\n", message, __LINE__); \
        failures++; \
    } else { \
        printf("PASS: %s\n", message); \
    } \
} while (0)

static int failures = 0;

struct HandshakeArgs {
    int fd;
    int result;
};

static void *receive_handshake_thread(void *arg)
{
    struct HandshakeArgs *args = arg;
    args->result = recv_handshake(args->fd);
    return NULL;
}

static void test_handshake(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Create sockets for handshake");
    if (failures && sockets[0] < 0)
        return;

    struct HandshakeArgs args = {sockets[1], 0};
    pthread_t thread;

    int created = pthread_create(
        &thread, NULL, receive_handshake_thread, &args);

    CHECK(created == 0, "Start handshake receiver thread");

    if (created == 0) {
        int sent = send_handshake(sockets[0], "TEST-PEER");
        pthread_join(thread, NULL);

        CHECK(sent == 1, "Send handshake");
        CHECK(args.result == 1, "Receive valid handshake");
    }

    close(sockets[0]);
    close(sockets[1]);
}

static void test_control_messages(void)
{
    int sockets[2];
    int id;
    uint32_t size;
    char buffer[WP_MAX_SIZE];

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Create sockets for control messages");

    struct {
        const char *name;
        int (*send_fn)(int);
        int expected_id;
    } tests[] = {
        {"CHOKE", send_choke, WP_CHOKE},
        {"UNCHOKE", send_unchoke, WP_UNCHOKE},
        {"INTERESTED", send_interested, WP_INTERESTED},
        {"NOT_INTERESTED", send_not_interested, WP_NOT_INTERESTED}
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int sent = tests[i].send_fn(sockets[0]);
        int received = recv_message(sockets[1], &id, buffer, &size);

        CHECK(sent == 1, tests[i].name);
        CHECK(received == 1 && id == tests[i].expected_id && size == 0,
              "Control message ID and empty payload");
    }

    close(sockets[0]);
    close(sockets[1]);
}

static void test_have_and_bitfield(void)
{
    int sockets[2];
    int id;
    uint32_t size;
    char buffer[WP_MAX_SIZE];

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Create sockets for HAVE and BITFIELD");

    CHECK(send_have(sockets[0], 7) == 1, "Send HAVE");

    int received = recv_message(sockets[1], &id, buffer, &size);
    uint32_t index = 0;

    if (received && size == 4)
        memcpy(&index, buffer, 4);

    CHECK(received && id == WP_HAVE && size == 4 &&
          ntohl(index) == 7, "Verify HAVE piece index");

    const char bits[] = {0xA0, 0x40};

    CHECK(send_bitfield(sockets[0], bits, sizeof(bits)) == 1,
          "Send BITFIELD");

    received = recv_message(sockets[1], &id, buffer, &size);

    CHECK(received && id == WP_BITFIELD &&
          size == sizeof(bits) &&
          memcmp(buffer, bits, sizeof(bits)) == 0,
          "Verify BITFIELD payload");

    close(sockets[0]);
    close(sockets[1]);
}

static void test_request_and_cancel(void)
{
    int sockets[2];
    int id;
    uint32_t size;
    char buffer[WP_MAX_SIZE];

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Create sockets for REQUEST and CANCEL");

    CHECK(send_request(sockets[0], 3, 128, 512) == 1,
          "Send REQUEST");

    int received = recv_message(sockets[1], &id, buffer, &size);
    uint32_t index = 0, begin = 0, length = 0;

    if (received && size == 12) {
        memcpy(&index, buffer, 4);
        memcpy(&begin, buffer + 4, 4);
        memcpy(&length, buffer + 8, 4);
    }

    CHECK(received && id == WP_REQUEST && size == 12 &&
          ntohl(index) == 3 &&
          ntohl(begin) == 128 &&
          ntohl(length) == 512,
          "Verify REQUEST fields");

    CHECK(send_cancel(sockets[0], 3, 128, 512) == 1,
          "Send CANCEL");

    received = recv_message(sockets[1], &id, buffer, &size);

    CHECK(received && id == WP_CANCEL && size == 12,
          "Verify CANCEL message");

    close(sockets[0]);
    close(sockets[1]);
}

static void test_piece(void)
{
    int sockets[2];
    int id;
    uint32_t size;
    char buffer[WP_MAX_SIZE];

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Create sockets for PIECE");

    const char content[] = "piece data";

    CHECK(send_piece(sockets[0], 2, 0,
                     content, sizeof(content) - 1) == 1,
          "Send PIECE");

    int received = recv_message(sockets[1], &id, buffer, &size);

    uint32_t index = 0, begin = 0;

    if (received && size >= 8) {
        memcpy(&index, buffer, 4);
        memcpy(&begin, buffer + 4, 4);
    }

    CHECK(received && id == WP_PIECE &&
          size == 8 + sizeof(content) - 1 &&
          ntohl(index) == 2 &&
          ntohl(begin) == 0 &&
          memcmp(buffer + 8, content, sizeof(content) - 1) == 0,
          "Verify PIECE index, offset and data");

    close(sockets[0]);
    close(sockets[1]);
}

static void test_keep_alive(void)
{
    int sockets[2];
    int id;
    uint32_t size;
    char buffer[WP_MAX_SIZE];

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Create sockets for KEEP-ALIVE");

    CHECK(send_keep_alive(sockets[0]) == 1, "Send KEEP-ALIVE");

    int received = recv_message(sockets[1], &id, buffer, &size);

    CHECK(received && id == -1 && size == 0,
          "Recognize KEEP-ALIVE");

    close(sockets[0]);
    close(sockets[1]);
}

static void test_metadata(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "Create sockets for METADATA");

    uint64_t file_size = 2500;
    uint32_t piece_size = 1024;
    uint32_t received_count = 0;
    uint32_t received_piece_size = 0;
    uint64_t received_file_size = 0;

    int sent = send_metadata(sockets[0], file_size, piece_size);

    int received = recv_metadata(
        sockets[1], &received_file_size,
        &received_piece_size, &received_count);

    CHECK(sent == 1, "Send METADATA");
    CHECK(received == 1, "Receive METADATA");
    CHECK(received_file_size == 2500 &&
          received_piece_size == 1024 &&
          received_count == 3,
          "Verify METADATA values");

    close(sockets[0]);
    close(sockets[1]);

    CHECK(send_metadata(-1, 100, 0) == 0,
          "Reject zero piece size");
    CHECK(recv_metadata(-1, NULL, &piece_size,
                        &received_count) == 0,
          "Reject NULL metadata output");
}

int main(void)
{
    printf("=== Wire Protocol Tests ===\n\n");

    test_handshake();
    test_control_messages();
    test_have_and_bitfield();
    test_request_and_cancel();
    test_piece();
    test_keep_alive();
    test_metadata();

    printf("\n=== Results: %d failure(s) ===\n", failures);

    return failures == 0 ? 0 : 1;
}
