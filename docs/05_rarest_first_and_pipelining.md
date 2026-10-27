# Feature 05: Rarest-First Piece Selection & Request Pipelining ($W=4$)

## 1. Overview and Purpose

A naive peer implementation downloads pieces sequentially (Piece 0, Piece 1, Piece 2...) or waits for each piece response before requesting the next one. This leads to two critical problems:
1. **The "Last Piece" & Swarm Starvation Problem**: Sequential downloading causes all peers to have identical pieces (e.g. all have Piece 0, none have Piece 50). If the original seeder goes offline, the swarm cannot finish downloading.
2. **Network Idle Latency (Stop-and-Wait Bottleneck)**: Waiting for a piece to arrive before issuing the next request wastes the entire Round-Trip Time (RTT) of the network link.

To solve these problems, our system implements:
- **Rarest-First Piece Selection**: Peers prioritize downloading pieces held by the fewest connected peers.
- **Request Pipelining ($W=4$)**: Each connection maintains up to 4 concurrent in-flight piece requests simultaneously.

---

## 2. Rarest-First Piece Selection Algorithm

### Where the code lives now

- `src/peer.c` owns the download loop and calls `choose_rarest_piece()` before
  sending each request.
- `src/rarest_first.c` contains only the rarest-first decision.
- `include/rarest_first.h` declares that decision for `peer.c`.

This split is intentional: the peer still handles sockets and file transfer,
while the selection policy can be explained as one small independent function.

### Why Rarest-First?
- **Maximizes Diversity**: Distributes rare pieces across multiple downloaders as quickly as possible.
- **Resilience Against Seeder Departure**: If the seeder leaves the swarm early, downloaders already hold complementary pieces and can finish downloading from each other.
- **Increases Swarm Upload Capacity**: More peers possess pieces that others want, maximizing symmetrical peer-to-peer sharing.

### Algorithm Implementation ([`src/peer.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/peer.c#L370-L414))

```c
/* For each active peer connection with available pipeline slots: */
while (peer->in_flight < PIPELINE_DEPTH) {
    int best_piece = -1;
    int min_avail = MAX_PEERS + 1;

    for (uint32_t i = 0; i < g_state.meta.piece_count; i++) {
        /* Only consider missing pieces that this peer possesses */
        if (g_state.piece_status[i] != PIECE_MISSING) continue;
        if (!BIT_CHECK(peer->bitfield, i)) continue;

        /* Count swarm-wide availability of piece i */
        int avail = 0;
        for (int q = 0; q < MAX_PEERS; q++) {
            if (g_state.peers[q].is_active && BIT_CHECK(g_state.peers[q].bitfield, i)) {
                avail++;
            }
        }

        /* Choose the piece held by the fewest peers */
        if (avail > 0 && avail < min_avail) {
            min_avail = avail;
            best_piece = (int)i;
        }
    }

    if (best_piece < 0) break; // No pieces currently available on this peer

    /* Mark as in-flight and issue request */
    g_state.piece_status[best_piece] = PIECE_IN_FLIGHT;
    g_state.piece_assigned_peer[best_piece] = p;
    peer->in_flight++;

    peer_send_request(peer, (uint32_t)best_piece);
}
```

---

## 3. Request Pipelining ($W=4$)

### Stop-and-Wait vs Pipelined Requests

```text
Stop-and-Wait (W=1):
Time  | Peer A (Downloader)             Peer B (Seeder)
0 ms  |--- REQUEST Piece 0 ------------->|
50 ms |                                 | (Reads from disk)
100 ms|<-- PIECE Piece 0 ---------------| (Transfers data)
      | [Network Link Idle during verification]
105 ms|--- REQUEST Piece 1 ------------->|
155 ms|                                 |
205 ms|<-- PIECE Piece 1 ---------------|
Total: ~100 ms per piece

Request Pipelining (W=4):
Time  | Peer A (Downloader)             Peer B (Seeder)
0 ms  |--- REQUEST Piece 0 ------------->|
1 ms  |--- REQUEST Piece 1 ------------->|
2 ms  |--- REQUEST Piece 2 ------------->|
3 ms  |--- REQUEST Piece 3 ------------->|
50 ms |                                 |<-- Pipeline Full (W=4)
100 ms|<-- PIECE Piece 0 ---------------| (Sends new request immediately!)
101 ms|--- REQUEST Piece 4 ------------->|
120 ms|<-- PIECE Piece 1 ---------------|
140 ms|<-- PIECE Piece 2 ---------------|
160 ms|<-- PIECE Piece 3 ---------------|
Throughput: Continuous stream, zero idle latency between pieces.
```

- In [`include/common.h`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/include/common.h#L27), `PIPELINE_DEPTH` is configured to `4`.
- Up to 4 requests can be in flight simultaneously per connected peer.
- As soon as one `MSG_PIECE` is received and verified, `peer->in_flight` is decremented, allowing the scheduler to immediately dispatch the next request.

---

## 4. Swarm Piece Sharing in Practice

Consider Seeder (S) and two Downloaders (D1, D2):
1. **D1** connects to **S** and requests Piece 0.
2. **D2** connects to **S** and requests Piece 1.
3. **D1** finishes Piece 0, verifies SHA-256, writes to disk, and sends `HAVE 0` to **D2**.
4. **D2** finishes Piece 1, verifies SHA-256, writes to disk, and sends `HAVE 1` to **D1**.
5. **D1 requests Piece 1 directly from D2!**
6. **D2 requests Piece 0 directly from D1!**
7. **Result:** S only uploaded 2 pieces total, yet both downloaders obtained both pieces. The upload burden is distributed across the swarm.

---

## 5. Viva Defense Questions & Answers

### Q1: What is the "Rarest-First" algorithm and why is it used in BitTorrent?
**Answer:**
Rarest-First selects the missing piece that is held by the fewest connected peers in the swarm. It avoids the bottleneck of everyone holding the same common pieces while rare pieces remain on only one peer. By aggressively propagating rare pieces across multiple downloaders, the swarm becomes resilient against seeders disconnecting.

### Q2: What is the difference between Rarest-First and Sequential downloading?
**Answer:**
Sequential downloading (e.g. streaming a video from start to finish) results in all downloaders having the beginning of the file and none having the end. If the seeder leaves, no downloader can finish. Rarest-First spreads different parts of the file evenly across downloaders, maximizing the number of complementary pieces that downloaders can trade with one another.

### Q3: What is Request Pipelining and why is the window size $W=4$?
**Answer:**
Request Pipelining allows a client to have multiple outstanding requests in transit simultaneously without waiting for the response to each before sending the next. If network round-trip time (RTT) is 50 ms, stop-and-wait would force the TCP connection to sit idle during every round trip. A pipeline depth of $W=4$ keeps the network pipe continuously filled with data, dramatically increasing transfer throughput.

### Q4: How does your implementation prevent requesting the same piece multiple times simultaneously?
**Answer:**
Each piece has a three-state lifecycle tracked in `g_state.piece_status[i]`:
1. `PIECE_MISSING (0)`: Not yet owned or requested.
2. `PIECE_IN_FLIGHT (1)`: Currently requested from a specific peer.
3. `PIECE_VERIFIED (2)`: Verified and written to disk.

The scheduler only selects pieces with status `PIECE_MISSING`. As soon as a request is sent, its status is immediately transitioned to `PIECE_IN_FLIGHT`. It will never be requested again unless the assigned peer disconnects before fulfilling the request.

### Q5: What happens if a piece request gets lost or a peer becomes unresponsive?
**Answer:**
If a peer disconnects or fails:
1. The connection is detected as dead in `peer_worker()`.
2. `disconnect_peer_locked()` is called.
3. Any piece assigned to that peer that was in `PIECE_IN_FLIGHT` is immediately reset to `PIECE_MISSING`.
4. The scheduler automatically reassigns it to another active peer that holds that piece.
