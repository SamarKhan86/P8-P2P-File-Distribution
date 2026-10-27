# Feature 03: Binary Wire Protocol & Packet Framing

## 1. Overview and Purpose

TCP provides a reliable, ordered **stream of bytes**, not discrete packets. A single `send()` call might be split into several IP packets, or multiple small messages might be coalesced together by Nagle's algorithm.

The Wire Protocol module ([`include/protocol.h`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/include/protocol.h) and [`src/protocol.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c)) defines:
1. Exact binary packet framing with length prefixes.
2. A fixed 60-byte handshake to validate swarm identity.
3. Strict message type definitions and byte-order conversions (`htonl`/`ntohl`).
4. Reliable I/O wrappers (`send_all()` and `recv_all()`) to guarantee complete message delivery and recover from POSIX interrupts (`EINTR`).

---

## 2. Packet Framing Specification

### 2.1 Fixed Handshake (60 Bytes)
The very first message exchanged across any newly established peer TCP connection:

```text
+-------------------+------------------------------+---------------------------+
| 8 Bytes (char[])  | 32 Bytes (uint8_t[])         | 20 Bytes (char[])         |
| Magic: "P2PLITE1" | File ID (SHA-256)            | Peer ID (e.g. "PEER_7001")|
+-------------------+------------------------------+---------------------------+
```

- **Magic (8 bytes):** ASCII `"P2PLITE1"`. Validates protocol version.
- **File ID (32 bytes):** Binary SHA-256 swarm identifier. Closes connection if mismatched.
- **Peer ID (20 bytes):** Unique alphanumeric ID identifying the peer instance.

### 2.2 Length-Prefixed Message Framing (Post-Handshake)
All messages following the handshake use length-prefixed binary framing:

```text
+-----------------------------------+--------------------+------------------------+
| 4 Bytes (uint32_t, big-endian)    | 1 Byte (uint8_t)   | N - 1 Bytes            |
| Length of message payload + ID    | Message ID         | Payload                |
+-----------------------------------+--------------------+------------------------+
```

Total bytes on wire = `4 + Length`.

---

## 3. Wire Message Types

| ID | Name | Payload Structure | Purpose |
|:---|:---|:---|:---|
| `0x01` | **`BITFIELD`** | `uint8_t[]` bits | Bit-packed array advertising all pieces possessed by the sender. |
| `0x02` | **`HAVE`** | `uint32_t piece_idx` | Informs peers that a new piece was verified and saved. |
| `0x03` | **`REQUEST`** | `uint32_t piece_idx` | Requests a complete 16 KB piece. |
| `0x04` | **`PIECE`** | `uint32_t piece_idx` + raw bytes | Delivers the requested piece index and its binary content. |
| `0x05` | **`CANCEL`** | `uint32_t piece_idx` | Cancels an outstanding piece request. |

### Helper Functions in [`src/protocol.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c):
- [`send_handshake()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L37-L46) / [`recv_handshake()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L48-L62)
- [`send_msg()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L64-L83) / [`recv_msg()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L85-L117)
- [`send_bitfield()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L119-L122)
- [`send_have()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L124-L128)
- [`send_request()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L130-L134)
- [`send_piece()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L136-L149)

---

## 4. Reliable Network I/O Implementation

### `send_all()`
Ensures that all bytes are transmitted even if the OS socket buffer only accepts partial bytes:
```c
int send_all(int fd, const void *buf, size_t len)
{
    const char *ptr = (const char *)buf;
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t sent = send(fd, ptr + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) continue; // Interrupted by system signal, retry
            return -1;                    // Hard socket error
        }
        if (sent == 0) return -1;         // Socket closed
        total_sent += (size_t)sent;
    }
    return 0;
}
```

### `recv_all()`
Guarantees reading the exact number of bytes requested:
```c
int recv_all(int fd, void *buf, size_t len)
{
    char *ptr = (char *)buf;
    size_t total_recv = 0;

    while (total_recv < len) {
        ssize_t n = recv(fd, ptr + total_recv, len - total_recv, 0);
        if (n < 0) {
            if (errno == EINTR) continue; // Retry on signal interrupt
            return -1;
        }
        if (n == 0) return -1;             // Peer disconnected gracefully
        total_recv += (size_t)n;
    }
    return 0;
}
```

---

## 5. Viva Defense Questions & Answers

### Q1: Why can't we just call `send(fd, buffer, size, 0)` once and assume all bytes were transmitted?
**Answer:**
TCP is a byte-stream transport with dynamic window sizing and finite socket buffers. A single `send()` call is only guaranteed to write what fits in the kernel's outgoing socket buffer at that instant (which may be less than requested). If `send()` returns a value smaller than `len`, the application must advance its pointer and call `send()` again until all bytes are transmitted. `send_all()` handles this in a loop.

### Q2: What is `EINTR` and why must socket loops check for it?
**Answer:**
`EINTR` (Interrupted System Call) occurs when a blocking system call (such as `recv` or `send`) is interrupted by a software signal (e.g. `SIGINT`, `SIGCHLD`) before completing. It is not an actual network error; the operation simply needs to be restarted immediately using `continue`.

### Q3: What is "message framing" and why is it necessary in TCP?
**Answer:**
TCP does not preserve message boundaries (unlike UDP datagrams). If a peer sends a 10-byte message followed by a 20-byte message, the receiver might read all 30 bytes in one `recv()` call, or read only 5 bytes. Without framing, the receiver cannot tell where one message ends and the next begins. We solve this using **length-prefixed framing**: the receiver always reads a fixed 4-byte header first, extracts `N`, and then reads exactly `N` payload bytes.

### Q4: Why do we use `htonl()` and `ntohl()` for integers on the network?
**Answer:**
Different CPU architectures represent multi-byte integers differently in memory:
- **x86/x64 / ARM (little-endian):** Least significant byte stored first.
- **Network standard (big-endian):** Most significant byte stored first.
To ensure that an integer like `piece_idx = 2` is interpreted identically regardless of hardware architecture, all multi-byte integers are converted to big-endian with `htonl()` (Host TO Network Long) before sending, and converted back with `ntohl()` (Network TO Host Long) upon receipt.

### Q5: Why can't we use null-terminated strings (`\0`) to delimit messages in our P2P system?
**Answer:**
File pieces contain **raw arbitrary binary data**. A compiled `.bin` file, JPEG image, or compressed archive will frequently contain the byte `0x00`. If we used null terminators, string functions like `strlen()` or `strtok()` would terminate early and corrupt the file transfer. All binary network packets must be handled by explicit byte count.
