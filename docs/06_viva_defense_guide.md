# Master Viva Defense Guide — BitTorrent Lite P2P

This guide contains the essential knowledge, architectural explanations, code navigation pointers, and the **top 25 viva questions** for defense and examination of this project.

---

## 1. The 30-Second Elevator Pitch

> *"Our project implements an educational, symmetrical BitTorrent-lite peer-to-peer file distribution system in C using POSIX sockets and pthreads with zero third-party dependencies. It divides arbitrary binary files into 16 KB chunks verified by individual SHA-256 hashes. Peers discover each other through a central tracker and exchange pieces directly over TCP. Downloaders simultaneously upload verified pieces to other peers using a rarest-first selection strategy and a 4-request pipeline, ensuring resilient, distributed swarm transfers without server bottlenecks."*

---

## 2. Code Navigation Cheat Sheet (Where is Everything?)

| Requirement | Source File & Function | Key Concept |
|:---|:---|:---|
| **Metainfo Creation** | [`src/metainfo.c: metainfo_create()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/metainfo.c#L17-L95) | Reads file, hashes 16 KB chunks, writes `.meta` |
| **SHA-256 Hashing** | [`src/sha256.c: sha256_buffer()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/sha256.c#L172-L181) | Pure C implementation of NIST SHA-256 |
| **Tracker Server** | [`src/tracker.c: main()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/tracker.c#L189-L250) | Central registry mapping `file_id` to `(ip, port)` |
| **Tracker Discovery** | [`src/tracker.c: tracker_announce()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/tracker.c#L26-L99) | Queries tracker and returns active peer list |
| **Wire Protocol Framing**| [`src/protocol.c: send_msg() / recv_msg()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L64-L117) | 4-byte length prefix + 1-byte ID + payload |
| **Reliable TCP I/O** | [`src/protocol.c: send_all() / recv_all()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/protocol.c#L3-L35) | Loops handling partial TCP transfers and `EINTR` |
| **Listener Thread** | [`src/peer.c: listener_thread()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/peer.c#L292-L329) | Accepts incoming peer connections on `my_port` |
| **Peer Worker Thread** | [`src/peer.c: peer_worker()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/peer.c#L125-L242) | Handles `REQUEST`, `PIECE`, `HAVE`, and uploads |
| **Rarest-First Scheduling**| [`src/peer.c: schedule_requests()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/peer.c#L370-L414) | Counts swarm rarity; dispatches requests |
| **Pipelining ($W=4$)** | [`include/common.h: PIPELINE_DEPTH`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/include/common.h#L27) | Up to 4 outstanding requests per connection |
| **Piece Verification** | [`src/peer.c: peer_worker()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/peer.c#L198-L233) | Hashes received piece before `pwrite()` & `HAVE` |
| **Disconnection Handling**| [`src/peer.c: disconnect_peer_locked()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/peer.c#L101-L123) | Reverts in-flight pieces back to `PIECE_MISSING` |

---

## 3. Top 25 Viva Questions & Model Answers

### Category A: Networking & Sockets

#### 1. Why did you use TCP instead of UDP?
**Answer:** File transfers require strict reliability and ordered data delivery. A single flipped or dropped byte corrupts an executable or compressed file. TCP provides built-in checksumming, packet ordering, retransmission, and flow control.

#### 2. What is the difference between stream sockets (`SOCK_STREAM`) and datagram sockets (`SOCK_DGRAM`)?
**Answer:** `SOCK_STREAM` (TCP) is connection-oriented and treats data as a continuous stream of bytes without message boundaries. `SOCK_DGRAM` (UDP) is connectionless and preserves message boundaries, but does not guarantee delivery or packet ordering.

#### 3. What are `send_all()` and `recv_all()` and why are they necessary?
**Answer:** In TCP, calling `send()` or `recv()` does not guarantee transmitting or receiving all requested bytes in one syscall. The OS socket buffer may only take partial data. `send_all()` and `recv_all()` loop until all bytes are transmitted or an error occurs.

#### 4. What is `SO_REUSEADDR` and why did you use it?
**Answer:** When a socket is closed, the operating system holds the local port in the `TIME_WAIT` state for up to two minutes to catch lingering packets. `setsockopt(..., SO_REUSEADDR, ...)` allows the peer or tracker to immediately re-bind to the same port when restarting without getting an `Address already in use` error.

#### 5. Why are network integers converted with `htonl()` and `ntohl()`?
**Answer:** Different computer architectures store numbers in little-endian or big-endian format. The network standard is big-endian. `htonl()` ensures that all multi-byte numbers (such as piece indexes and message lengths) are transmitted in big-endian format regardless of the host machine's architecture.

---

### Category B: Concurrency & Operating Systems

#### 6. Explain the threading model of your peer.
**Answer:** Each peer process has:
1. A **Listener Thread** that accepts incoming connections.
2. Multiple **Peer Worker Threads** (one per connected peer) that read messages, upload pieces, and verify downloaded pieces.
3. The **Main Thread** which queries the tracker, connects to discovered peers, and runs the rarest-first request scheduler.

#### 7. What is a mutex and where is it used in your project?
**Answer:** A mutex (`pthread_mutex_t`) is a mutual exclusion lock used to prevent race conditions when multiple threads access shared resources. In our peer, `state_mutex` protects the peer connection list, bitfield arrays, and piece status arrays. `send_mutex` serializes writes to individual socket descriptors.

#### 8. Why are `pread()` and `pwrite()` preferred over `read()`, `write()`, and `lseek()`?
**Answer:** Standard `read()` and `write()` update the shared file offset pointer in the kernel file table. If multiple threads read and write different pieces concurrently, `lseek()` calls would race and corrupt the file. `pread()` and `pwrite()` take explicit byte offsets and do not modify the file table's offset, making them thread-safe.

#### 9. How do you handle graceful shutdown when a user presses Ctrl+C?
**Answer:** A signal handler catches `SIGINT` and sets `g_state.running = 0`. The main loop and listener thread break cleanly, close all open sockets, close the file descriptor, and free allocated memory before exiting.

#### 10. How does `ftruncate()` work when creating a new file?
**Answer:** When a downloader starts, it calls `ftruncate(file_fd, meta.file_size)`. This immediately allocates the file to its full final size on disk, allowing out-of-order pieces to be written directly to their target byte offsets via `pwrite()`.

---

### Category C: P2P Protocol & Algorithms

#### 11. What is the role of the Tracker? Does it store file data?
**Answer:** The Tracker is a central rendezvous directory for peer discovery. It maps `file_id` to a list of participating `(ip, port)` endpoints. It **never** receives, routes, or stores file pieces.

#### 12. What is Rarest-First piece selection?
**Answer:** When choosing which missing piece to download next, the peer inspects the bitfields of all connected peers and selects the piece held by the fewest peers.

#### 13. Why is Rarest-First superior to Sequential downloading?
**Answer:** Sequential downloading causes all peers to have the beginning of the file and none to have the end. If the seeder disconnects, the swarm starves. Rarest-First maximizes piece diversity across peers, allowing them to trade complementary pieces and complete downloads even if the seeder leaves early.

#### 14. What is Request Pipelining?
**Answer:** Request Pipelining allows a peer to maintain multiple outstanding piece requests (up to $W=4$) simultaneously without waiting for each piece before sending the next request. This eliminates network idle time caused by Round-Trip Time (RTT).

#### 15. What are `BITFIELD` and `HAVE` messages?
**Answer:**
- `BITFIELD` is sent right after the handshake and contains a bitmask indicating all pieces currently possessed by the sender.
- `HAVE` is a 4-byte broadcast message sent to all connected peers whenever a peer finishes verifying a new piece.

---

### Category D: Cryptography & Data Integrity

#### 16. Why do you hash each piece individually instead of hashing only the entire file?
**Answer:** Hashing per piece enables **immediate verification upon arrival**. If a piece fails the SHA-256 check, it is immediately discarded and re-requested, without needing to re-download the entire file.

#### 17. How does the system prevent piece poisoning (corrupted data)?
**Answer:** When `MSG_PIECE` arrives, the data is not immediately written to disk. The peer computes its SHA-256 digest using `sha256_buffer()` and compares it to the expected hash in the metadata. Only if they match byte-for-byte is the piece written to disk via `pwrite()`.

#### 18. What is the `file_id`?
**Answer:** The `file_id` is a 32-byte SHA-256 hash computed over the concatenation of all individual piece hashes. It acts as a tamper-proof fingerprint for the entire transfer.

#### 19. What happens during the Handshake?
**Answer:** Both peers exchange a 60-byte binary packet containing:
1. Magic string `"P2PLITE1"` (8 bytes)
2. `file_id` (32 bytes)
3. `peer_id` (20 bytes)
If the magic string or `file_id` does not match, the socket is immediately closed.

#### 20. Can an attacker alter the file without detection?
**Answer:** No. Changing even a single bit in a piece alters its SHA-256 digest with overwhelming probability (collision resistance). The piece verification check will fail and the corrupted data will be rejected.

---

### Category E: Practical Scenarios & Edge Cases

#### 21. What happens if a peer disconnects while a request is in flight?
**Answer:** The peer worker detects socket EOF/error, calls `disconnect_peer_locked()`, closes the socket, and resets all pieces that were `PIECE_IN_FLIGHT` with that peer back to `PIECE_MISSING`. The scheduler will assign them to another peer on the next tick.

#### 22. What happens if two downloaders finish at different times?
**Answer:** The first downloader to finish enters a seeding period (configurable via `--seed-time`) so the second downloader can retrieve remaining pieces from it before it exits.

#### 23. What if a duplicate piece arrives?
**Answer:** In `peer_worker()`, if `g_state.piece_status[piece_idx] == PIECE_VERIFIED`, the piece is recognized as a duplicate and ignored without redundant disk writes.

#### 24. What happens if the input file size is not an exact multiple of the piece size?
**Answer:** `metainfo_piece_len()` calculates `file_size % piece_size` for the last piece index (`piece_count - 1`). The last piece is hashed, transferred, and stored according to its exact remaining length.

#### 25. How do you prove that the downloaded file is 100% correct?
**Answer:** By running the Linux `cmp` utility against the original source file:
```bash
cmp sample.bin downloaded.bin
```
If `cmp` exits with code 0 and produces no output, the files are confirmed to be byte-for-byte identical.
