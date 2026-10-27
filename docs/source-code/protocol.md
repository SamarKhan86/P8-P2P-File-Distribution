# `src/protocol.c` — custom TCP wire protocol

## Purpose in one sentence

`protocol.c` turns application-level peer messages into framed byte sequences and reads those frames reliably from TCP connections.

## Why this file exists

TCP is a byte stream: one `send()` does not guarantee that a whole message arrives in one `recv()`. The application needs a known message boundary and a way to identify the message type. This file centralizes those framing rules so peer logic does not need to repeat partial-read and partial-write loops.

## Where it fits

Declarations and message IDs are in [`include/protocol.h`](../../include/protocol.h). `peer_connections.c` uses the handshake functions. `peer_transfer.c` uses `send_msg()` and `recv_msg()` through the helpers and dispatches the resulting message IDs.

## Wire format

### Handshake

The handshake is exactly 60 bytes:

| Offset | Size | Content |
|---:|---:|---|
| 0 | 8 bytes | Magic string `P2PLITE1` |
| 8 | 32 bytes | `file_id` bytes |
| 40 | 20 bytes | Peer ID, padded/truncated to 20 bytes |

This is a project-specific handshake, not the standard BitTorrent handshake. The receiver rejects an incorrect magic value; the connection module separately checks that the file ID matches.

### Ordinary message

```text
+----------------------+----------------+-------------------+
| Length (4 bytes)     | ID (1 byte)    | Payload (N bytes) |
+----------------------+----------------+-------------------+
```

The four-byte length is in network byte order and counts the message ID byte plus payload bytes (it does not include the four-byte length field itself). All standard integer fields for piece indexes are converted using `htonl()` before sending and `ntohl()` after receiving.

## Message IDs in `include/protocol.h`

| Name | ID | Payload in this implementation |
|---|---:|---|
| `MSG_BITFIELD` | 1 | Raw bitfield bytes; bit ordering uses `BIT_SET`/`BIT_CHECK` in `common.h`. |
| `MSG_HAVE` | 2 | One 32-bit network-order piece index. |
| `MSG_REQUEST` | 3 | One 32-bit network-order piece index. The request asks for the whole piece. |
| `MSG_PIECE` | 4 | A 32-bit piece index followed by the whole piece's data bytes. |
| `MSG_CANCEL` | 5 | Defined as an ID, but the transfer worker currently does not implement cancel behavior. |

There is no separate “complete” message. A peer announces a newly available piece with `HAVE`, while completion is determined locally by counting verified pieces.

## Functions

### `int send_all(int fd, const void *buf, size_t len)`

Loops until all requested bytes are sent. It advances a pointer after partial sends, retries if interrupted by `EINTR`, and returns `-1` on other send errors or if `send()` returns zero. Returns `0` only after sending every byte.

### `int recv_all(int fd, void *buf, size_t len)`

The receive counterpart. It loops until exactly `len` bytes arrive, retries on `EINTR`, and treats EOF/disconnection or another receive error as failure. This is important for reading a fixed-size handshake or header.

### `int send_handshake(...)`

Builds the 60-byte handshake: copies the eight-byte magic string and 32-byte file ID, zeroes the 20-byte peer-ID region, copies up to 20 bytes of peer ID, then uses `send_all()`.

### `int recv_handshake(...)`

Reads all 60 bytes with `recv_all()`, checks the magic bytes, and copies out the file ID and 20-byte peer ID. The destination peer-ID buffer should have room for a trailing null if the caller intends to treat it as a C string; the connection module uses a 21-byte array and zeroes it first.

### `int send_msg(int fd, uint8_t msg_id, const void *payload, uint32_t payload_len)`

Computes `total_len = 1 + payload_len`, converts it to network byte order, allocates a buffer for the four-byte length plus ID plus payload, fills it, sends the whole frame with `send_all()`, frees the temporary buffer, and returns the send result.

### `int recv_msg(int fd, uint8_t *msg_id, void **payload, uint32_t *payload_len)`

Reads the four-byte length, converts it to host order, and rejects a length smaller than one or larger than 64 MiB. It then reads one ID byte and reads the remaining payload into a newly allocated buffer. For an empty payload, it returns a null payload pointer. If payload receipt fails, it frees the allocation and returns `-1`. The caller is responsible for freeing a successful non-null payload.

### `send_bitfield()`, `send_have()`, `send_request()`, `send_piece()`

Convenience wrappers encode the known message ID and payload shape. `send_have()` and `send_request()` encode a 32-bit index. `send_piece()` allocates a payload containing the index plus piece bytes before calling `send_msg()`.

## Why the length prefix matters

TCP may split one frame into multiple reads or deliver several frames' bytes near one another. `recv_msg()` first learns the frame size, then reads the exact ID and payload lengths. The `send_all()`/`recv_all()` loops make the application independent of the exact size of each socket call.

## Failure cases and limitations

- A peer disconnecting halfway through a frame causes the receive helper to fail.
- Frames with invalid lengths (zero ID-plus-payload length or over 64 MiB) are rejected.
- Unknown but length-valid message IDs are left to `peer_transfer.c` to handle.
- The protocol uses fixed 32-bit piece indexes and a 64 MiB frame-size limit.
- `MSG_CANCEL` exists in the constants but is not acted upon by the worker.
- This protocol does not implement the full set of standard BitTorrent wire messages.

## Likely viva questions

**Q: Why can't you assume one `recv()` returns the full message?**

A: TCP has no application message boundaries. It can return fewer bytes than requested, so the code loops until the exact frame length is available.

**Q: Why is the length sent in network byte order?**

A: Network byte order makes multi-byte integers have a consistent byte representation between machines with different CPU byte orders.

**Q: Why check the file ID in addition to handshake magic?**

A: The magic only tells us the other side speaks this protocol. The file ID checks that it wants to share the same file/swarm.

## Where to look next

- [`peer_connections.md`](peer_connections.md) — handshake validation in a real connection.
- [`peer_transfer.md`](peer_transfer.md) — message-ID dispatch and payload interpretation.
- [`headers-and-shared-types.md`](headers-and-shared-types.md) — constants and bitfield macros.
