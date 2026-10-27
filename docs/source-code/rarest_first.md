# `src/rarest_first.c` — rarest-first piece selector

## Purpose in one sentence

`rarest_first.c` chooses a missing piece that a particular connected peer can provide, preferring candidates advertised by the fewest active peers.

## Why this file exists

Piece selection is an algorithmic decision that can be explained and tested separately from sockets and file I/O. Keeping the selection in its own small function makes it easier to see the policy and avoids embedding a long search directly in the scheduler.

## Where it fits

The function is declared in [`include/rarest_first.h`](../../include/rarest_first.h) and called by `peer.c:schedule_requests()`. It reads peer and piece state from the structures in `include/peer.h`; it does not send network messages or modify piece status itself.

## Function: `int choose_rarest_piece(const peer_state_t *peer_state, const peer_conn_t *source_peer)`

### Inputs and output

- `peer_state`: overall local peer state, including each piece's status and the table of active peers.
- `source_peer`: the connection to which the scheduler is considering sending a request.
- Return: the selected piece index, or `-1` if this source peer cannot provide any currently missing piece.

The header contract says the caller must hold `peer_state->state_mutex` while calling this function, because the fields it reads are shared with worker threads.

### Algorithm

1. Start with no selected piece (`-1`) and a best rarity count larger than the allowed peer count.
2. Iterate through every piece.
3. Skip pieces that are not `PIECE_MISSING`.
4. Skip pieces that the given `source_peer` does not advertise in its bitfield.
5. Count active peer connections whose bitfield advertises the candidate piece.
6. If the count is positive and smaller than the best count seen, remember this piece.
7. Return the remembered index.

Because the comparison is strictly `<`, the first candidate with a given count wins. This is a simple deterministic tie-break based on piece index traversal order.

## Important nuance in this implementation

This is a simplified, per-source selection: it only chooses a piece advertised by `source_peer`, and counts source availability across active peer slots. The scheduler calls it separately for each connected source. The selector does not globally choose one swarm-wide piece and assign it to any peer, does not randomize equal-rarity choices, and does not directly manage in-flight requests. The scheduler handles assignment and pipelining.

## Example

Suppose the downloader still needs pieces 1, 2, and 4. The selected source advertises all three. Across active peers, piece 1 is advertised by one peer, piece 2 by two peers, and piece 4 by three peers. This function selects piece 1 for that source because it has the fewest active sources. If the selected source does not have piece 1, it is ineligible and the algorithm can choose the rarest among pieces it does have.

## Complexity

Let `P` be the piece count and `N` be `MAX_PEERS`. For each of up to `P` candidates, the function scans up to `N` peer slots, so the time complexity is `O(P × N)` per selection call. With the current maximum of 32 peer slots, this is a straightforward implementation rather than an optimized rarity index.

## Limitations

- It counts only currently active connections and the bitfields currently stored in memory.
- The peer's bitfield can be temporarily out of date until a `BITFIELD` or `HAVE` message is processed.
- In-flight reservations are excluded by requiring `PIECE_MISSING`.
- It does not implement choking, endgame mode, randomized tie-breaking, or a global planner.

## Likely viva questions

**Q: What is the intuition behind rarest-first?**

A: Download pieces that have fewer sources first, reducing the risk that a piece available from only one peer becomes a bottleneck if that peer disappears.

**Q: Does this function send the request?**

A: No. It only returns a piece index. `schedule_requests()` marks it in-flight, records the assigned peer, and calls `peer_send_request()`.

**Q: What does `-1` mean?**

A: The particular source connection has no piece that is both advertised by it and currently missing locally.

## Where to look next

- [`peer.md`](peer.md) — `schedule_requests()` and pipelining.
- [`peer_connections.md`](peer_connections.md) — how the remote bitfield is initialized.
- [`peer_transfer.md`](peer_transfer.md) — how `BITFIELD` and `HAVE` update availability.
