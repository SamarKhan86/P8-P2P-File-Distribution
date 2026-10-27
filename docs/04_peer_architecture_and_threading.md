# Feature 04: Symmetrical Peer Architecture & Pthread Concurrency

## 1. Overview and Purpose

A fundamental principle of BitTorrent is **symmetrical participation**: every downloading peer contributes to uploading verified pieces to other peers in the swarm.

The unified peer module ([`src/peer.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/peer.c)) eliminates the client/server divide. A single binary executable can:
1. Act as an initial **Seeder** (possessing 100% verified pieces).
2. Act as a concurrent **Downloader** (acquiring missing pieces while actively uploading verified pieces).
3. Handle concurrent network communication using POSIX threads (`pthreads`) without blocking file disk I/O or network transfers.

---

## 2. Multi-Threaded Process Model

The peer process employs a clean, robust, multi-threaded architecture:

```text
                               +-----------------------------+
                               |     Peer Main Process       |
                               +--------------+--------------+
                                              |
        +-------------------------------------+-----------------------------------+
        |                                     |                                   |
        v                                     v                                   v
+-----------------------+           +-----------------------+           +-----------------------+
|    Listener Thread    |           |  Main Scheduler Loop  |           | Peer Workers (1 / TCP)|
| (Accepts Inbound TCP) |           | (Rarest-First, Pipes) |           | (Reads Wire Messages) |
+-----------+-----------+           +-----------+-----------+           +-----------+-----------+
            |                                   |                                   |
            | (New peer connected)              | (Requests pieces)                 | (Uploads & Verifies)
            +----------------------------------->===================================+
                                                |  Protected by g_state.state_mutex |
                                                +-----------------------------------+
```

### 1. Listener Thread (`listener_thread`)
- Listens on `my_port` via `bind()` and `listen()`.
- Executes `accept()` to receive incoming connections from other peers.
- Performs mutual handshake exchange and registers the new peer connection.

### 2. Peer Worker Threads (`peer_worker`)
- Spawned for each active peer connection (inbound or outbound).
- Runs an independent read loop calling `recv_msg()`.
- When receiving `MSG_REQUEST`: reads verified piece data from disk via `pread()` and sends `MSG_PIECE`.
- When receiving `MSG_PIECE`: validates SHA-256 digest, writes to disk with `pwrite()`, and broadcasts `HAVE` to all peers.

### 3. Main Scheduler Thread (`main`)
- Periodically announces to the Tracker to discover newly joined peers.
- Initiates outbound connections to discovered peers.
- Runs the **rarest-first piece scheduler** and manages request pipelining ($W=4$).
- Monitors download completion and handles graceful termination.

---

## 3. Concurrency & Synchronization Primitives

### Shared State Protection (`state_mutex`)
All modifications to the following shared structures are guarded by `pthread_mutex_lock(&g_state.state_mutex)`:
- `peers[]`: connection array and per-peer in-flight counters.
- `my_bitfield[]`: local piece possession bitmask.
- `piece_status[]`: piece states (`PIECE_MISSING`, `PIECE_IN_FLIGHT`, `PIECE_VERIFIED`).
- `piece_assigned_peer[]`: mapping of pending pieces to peer connection indices.

### Socket Write Serialization (`send_mutex`)
Because multiple threads might send data to the same socket (e.g. main scheduler sending `REQUEST`, worker thread sending `PIECE`, and broadcast sending `HAVE`), each connection has a dedicated `send_mutex`:
```c
static int peer_send_msg(peer_conn_t *peer, uint8_t msg_id, const void *payload, uint32_t payload_len) {
    pthread_mutex_lock(&peer->send_mutex);
    int ret = send_msg(peer->sock_fd, msg_id, payload, payload_len);
    pthread_mutex_unlock(&peer->send_mutex);
    return ret;
}
```

### Atomic File I/O (`pread` / `pwrite`)
The file descriptor `g_state.file_fd` is shared across all threads. Standard `read()` and `write()` modify the kernel's internal file pointer (`lseek`), which would cause catastrophic race conditions when multiple threads read and write different pieces.

Instead, we use POSIX **`pread()`** and **`pwrite()`**:
```c
pwrite(g_state.file_fd, data, data_len, (off_t)piece_idx * g_state.meta.piece_size);
pread(g_state.file_fd, buf, piece_len, (off_t)piece_idx * g_state.meta.piece_size);
```
`pread` and `pwrite` are **atomic** with respect to file offsets and do not modify the file table's offset pointer, making them completely thread-safe across concurrent reader and writer threads.

---

## 4. Fault Tolerance & Disconnection Recovery

If a remote peer disconnects or experiences a network error during transfer:
```c
static void disconnect_peer_locked(int peer_idx)
{
    peer_conn_t *p = &g_state.peers[peer_idx];
    if (p->sock_fd >= 0) {
        close(p->sock_fd);
        p->sock_fd = -1;
    }
    p->is_active = 0;
    p->in_flight = 0;

    /* Revert in-flight piece assignments */
    for (uint32_t i = 0; i < g_state.meta.piece_count; i++) {
        if (g_state.piece_assigned_peer[i] == peer_idx && g_state.piece_status[i] == PIECE_IN_FLIGHT) {
            g_state.piece_status[i] = PIECE_MISSING;
            g_state.piece_assigned_peer[i] = -1;
        }
    }
}
```
Any pieces that were marked `PIECE_IN_FLIGHT` with that peer immediately revert to `PIECE_MISSING`. On the very next scheduler tick, another peer will fulfill the request.

---

## 5. Viva Defense Questions & Answers

### Q1: What makes your peer architecture "symmetrical"?
**Answer:**
In traditional client-server systems, there are separate server programs (which only upload) and client programs (which only download). In our symmetrical architecture, a single `peer` process runs both a TCP server (listening on its port) and TCP clients (connecting to other peers). While downloading missing pieces from peer A, it concurrently serves already-verified pieces to peer B.

### Q2: What is a race condition, and how does your code prevent them?
**Answer:**
A race condition occurs when two threads concurrently read and modify shared memory without synchronization, leading to corrupted state (such as requesting the same piece twice or writing inconsistent bitfields). We prevent race conditions using `g_state.state_mutex`. Critical sections—such as selecting a missing piece, updating `piece_status`, or incrementing `verified_count`—are strictly serialized inside mutex lock/unlock blocks.

### Q3: Why did you use `pwrite()` and `pread()` instead of standard `write()` and `read()`?
**Answer:**
Standard `read()` and `write()` rely on a single shared file offset pointer stored in the kernel's open file description. If Thread A calls `lseek()` to write Piece 1, and Thread B calls `lseek()` to read Piece 5 before Thread A finishes writing, Thread A will write data at the wrong offset. `pread()` and `pwrite()` take an explicit file offset as an argument and do not modify the system file pointer, allowing multiple threads to safely read and write different pieces in parallel.

### Q4: What happens if a peer crashes or disconnects while downloading a piece?
**Answer:**
When the socket closes or fails, `recv_msg()` returns an error. The peer worker thread immediately invokes `disconnect_peer_locked()`. All pieces assigned to that peer with status `PIECE_IN_FLIGHT` are automatically reset to `PIECE_MISSING`, and the socket is closed. The main scheduler will reassign those pieces to other active peers in the next cycle.

### Q5: How is graceful shutdown handled on `SIGINT` (Ctrl+C)?
**Answer:**
A signal handler intercepts `SIGINT` and sets `g_state.running = 0`. The main loop and listener thread break out of their cycles, close all active client sockets and the listening socket, close the data file descriptor with `close(g_state.file_fd)`, and free all dynamically allocated memory.
