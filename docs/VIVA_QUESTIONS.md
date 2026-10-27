# Viva questions and answers

## Project overview

1. **Core — What does this project do?** It splits one file into pieces and lets localhost peer processes discover and exchange those pieces over TCP.
2. **Core — Why is it peer-to-peer?** The tracker only gives addresses; peers send file bytes directly to peers.
3. **Core — Is this official BitTorrent?** No. It is a small custom protocol inspired by BitTorrent.
4. **What is one transfer's scope?** One arbitrary binary file described by one `.meta` file.
5. **Why split a file?** Different peers can upload different verified pieces concurrently.
6. **What are the executables?** `metainfo`, `tracker`, and `peer`.
7. **What is a seeder here?** A peer with every verified piece.
8. **Can a downloader upload?** Yes, after it verifies a piece.

## Architecture and files

9. **Core — What coordinates a peer?** `src/peer.c`: options, state setup, discovery, scheduling, completion and cleanup.
10. **Why is `peer_connections.c` separate?** It isolates accept/connect/handshake/disconnect lifecycle.
11. **Why is `peer_transfer.c` separate?** It handles peer messages and serialized sends.
12. **What does `piece_manager.c` do?** It reads local pieces and identifies valid existing pieces at startup.
13. **What does `protocol.c` do?** It turns TCP bytes into complete custom messages.
14. **What does `rarest_first.c` do?** Only selects a candidate piece; it does not send it.
15. **Why use headers?** They declare shared types/functions without duplicating implementations.
16. **What owns shared peer state?** `peer_state_t` in `peer.c`, exposed to the peer modules by `peer_runtime.h`.

## Networking and tracker

17. **Core — Why TCP?** It gives ordered, reliable bytes; the application still must frame messages.
18. **What do `bind`, `listen`, and `accept` do?** Reserve a local port, queue connections, and obtain an inbound socket.
19. **What does `connect` do?** It opens an outbound TCP connection to a known endpoint.
20. **Why is one `recv` insufficient?** TCP may return only part of a logical message.
21. **What do `send_all` and `recv_all` do?** Loop until all requested bytes transfer or an error/disconnect occurs.
22. **What does the tracker store?** In-memory file-ID to IP/port registrations.
23. **What does it not store?** File pieces and completed output files.
24. **How often does a peer announce?** About every three seconds in `peer.c`.

## Protocol

25. **Core — What is in the handshake?** Magic `P2PLITE1`, 32-byte file ID, and 20-byte peer ID: 60 bytes total.
26. **Why compare file IDs?** It rejects a connection from a different transfer.
27. **How are normal messages framed?** Four-byte big-endian length, one-byte type, then payload.
28. **What does BITFIELD mean?** The sender's bitmap of currently verified pieces.
29. **What does HAVE mean?** A peer has just verified one piece.
30. **What does REQUEST mean?** Send this complete piece index.
31. **What does PIECE contain?** Network-order piece index followed by raw piece bytes.
32. **Is CANCEL implemented?** Its ID exists, but worker handling is intentionally empty.

## Pieces, integrity, and selection

33. **Core — How is the last piece handled?** `metainfo_piece_len` returns the remaining bytes rather than full piece size.
34. **How is the write offset found?** `piece_index * piece_size`, passed to `pwrite`.
35. **Why use `pwrite`?** It writes to the correct offset without moving a shared file cursor.
36. **When is a piece shareable?** Only after SHA-256 matches the metadata hash.
37. **What happens to a bad hash?** The data is discarded and status returns to missing.
38. **What do MISSING, IN_FLIGHT, VERIFIED mean?** Needed, requested but not finished, and validated/available.
39. **Core — What is rarest-first?** Choose a needed piece present at the fewest active peers.
40. **Why can it help?** It spreads scarce pieces before their only source disappears.
41. **How are ties resolved?** The first equal-count candidate stays selected.
42. **What is pipelining?** Up to `PIPELINE_DEPTH` (four) whole-piece requests per connection can wait for replies.

## Concurrency and logging

43. **Core — Which threads exist?** Main, one listener, and one detached worker per active peer connection.
44. **What does `state_mutex` protect?** Bitfields, statuses, assignments, counts and connection state.
45. **Why is there a send mutex per peer?** Two threads must not interleave bytes of separate messages on one socket.
46. **What happens on disconnect?** The socket closes and its assigned in-flight pieces become missing again.
47. **What is logged?** Connections, requests, receipt, verification/failure, uploads and completion.
48. **Difference: received vs verified?** Received means bytes arrived; verified means their SHA-256 matched.
49. **How is a successful upload identified?** `piece_uploaded` is emitted only after `peer_send_piece` succeeds.
50. **Where are test logs?** `logs/runs/<run_id>/peer_<port>.csv` and `tracker.log`.

## Testing, trade-offs, and troubleshooting

51. **Core — What does `cmp` prove?** The downloaded and original files have identical bytes.
52. **What does a passing test not prove?** Internet-scale reliability, security, or full BitTorrent compatibility.
53. **How does the test prove P2P sharing?** It stops the original seeder and checks downloader 17002 logs/uploads pieces to 17003.
54. **What if a port is occupied?** `bind` fails; choose unused ports or stop the conflicting process.
55. **What if tracker discovery returns nobody?** Check tracker IP/port, shared metadata file ID, and that peers announced.
56. **What if a download stalls?** Inspect peer connection logs and bitfields; disconnected assigned pieces should be retried.
57. **What if `cmp` differs?** Check completion/logged hash failures and regenerate metadata from the intended source.
58. **Why no DHT/NAT traversal?** The educational scope is same-machine localhost discovery.
59. **Why no choking?** It is deliberately out of scope; all verified pieces can be uploaded.
60. **Why no encryption/authentication?** The project assumes cooperative localhost peers; hashes provide integrity, not identity.

## Spoken answer and demo

**60–90 second explanation:** “This is a simplified BitTorrent-style file distribution system in C. First, metainfo creates a file ID and SHA-256 hash for every fixed-size piece. A tracker stores only peer addresses for that ID. Each peer listens for TCP connections, discovers others through the tracker, exchanges a handshake and bitfield, then requests missing pieces. The scheduler uses rarest-first selection and keeps up to four whole-piece requests outstanding per connection. When a piece arrives, the peer hashes it, writes it at its indexed offset only if the hash matches, and broadcasts HAVE so it can upload that verified piece too. The design splits coordination, connections, transfer messages, local-piece operations, protocol framing, hashing, and logging into small C files. Our integration test checks byte-for-byte output with cmp and demonstrates a downloader uploading to another downloader.”

**Live demo checklist:** In Ubuntu WSL run `make clean && make all && make test`; observe tracker, seeder, both downloaders, `cmp` success, and a `piece_uploaded` row in the retained run directory.

**Honest limitations answer:** “It is intentionally a localhost educational implementation. It has a custom protocol, one-file transfers, a fixed peer limit, no persistence/authentication/encryption/NAT traversal/choking, simple deterministic tie breaking, and only basic reconnect behavior. I would next improve robust shutdown, tracker expiry, richer failure recovery, and broader tests before considering public-network features.”
