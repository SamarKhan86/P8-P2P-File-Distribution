# Source-code guide

This directory explains the **actual C source files** in the supplied P2P BitTorrent Lite repository. It is written to help a beginner follow the code during development and a college viva.

## Source-file index

| Source file | Why it exists | Main responsibilities |
|---|---|---|
| [`peer.c`](peer.md) | Coordinates one peer process. | Parses arguments, initializes shared state, creates the listening socket, announces to the tracker, schedules requests, detects completion, and shuts down. |
| [`peer_connections.c`](peer_connections.md) | Keeps connection setup and cleanup out of the coordinator. | Accepts incoming peers, initiates outgoing connections, exchanges handshakes, registers connections, and resets in-flight assignments on disconnect. |
| [`peer_transfer.c`](peer_transfer.md) | Handles the messages exchanged over an established peer connection. | Sends peer messages, runs one worker thread per connection, serves requested pieces, receives and verifies pieces, and announces new pieces with `HAVE`. |
| [`piece_manager.c`](piece_manager.md) | Centralizes small file-piece operations. | Determines a piece length, reads a piece for upload, and scans an existing output file for pieces that already verify. |
| [`rarest_first.c`](rarest_first.md) | Keeps the piece-selection policy independently understandable. | Chooses a missing piece offered by a particular peer, preferring pieces with fewer active sources. |
| [`protocol.c`](protocol.md) | Separates byte-level TCP framing from peer behavior. | Reads/writes complete byte sequences, exchanges the handshake, encodes and decodes messages, and provides message helpers. |
| [`metainfo.c`](metainfo.md) | Describes the file being shared independently of the live transfer. | Creates and loads metadata, stores expected piece hashes, reports metadata, and provides a CLI when built with `METAINFO_CLI`. |
| [`sha256.c`](sha256.md) | Provides hashing without an external crypto library. | Implements SHA-256 and exposes a one-buffer convenience function. |
| [`tracker.c`](tracker.md) | Implements peer discovery. | Converts hashes to/from hexadecimal, announces to a tracker, and—when built with `TRACKER_MAIN`—runs the tracker server. |
| [`logger.c`](logger.md) | Keeps optional transfer-event CSV writing out of networking logic. | Opens a CSV, writes a consistent event row under a mutex, and closes the log. |

There are **10 `.c` files** in `src/` in the supplied archive. This index has one matching document for each of them. There is no `peer_runtime.c`; shared runtime symbols are declared in [`peer_runtime.h`](../../include/peer_runtime.h) and defined in `peer.c`.

## How to read the project

A good first pass is:

1. Read [`peer.md`](peer.md) to understand how the process starts and coordinates work.
2. Read [`peer_connections.md`](peer_connections.md) and [`peer_transfer.md`](peer_transfer.md) to follow connection setup and the per-connection worker.
3. Read [`protocol.md`](protocol.md) to understand the actual bytes sent over TCP.
4. Read [`metainfo.md`](metainfo.md), [`sha256.md`](sha256.md), [`piece_manager.md`](piece_manager.md), and [`rarest_first.md`](rarest_first.md) to understand file identity, integrity, file operations, and piece selection.
5. Read [`tracker.md`](tracker.md) and [`logger.md`](logger.md) for peer discovery and observability.
6. Use [`headers-and-shared-types.md`](headers-and-shared-types.md) whenever a source file refers to a type or declaration you cannot find locally.

## Important implementation boundaries

- This is a simplified BitTorrent-inspired protocol, not the complete BitTorrent specification.
- The tracker helps peers discover each other; piece bytes move directly between peers.
- The project uses TCP and whole-piece requests. A request is not subdivided into smaller blocks.
- SHA-256 is used to detect piece-content mismatches against the hashes in the metadata.
- `rarest_first.c` chooses among pieces that the selected source peer advertises. It counts how many currently active peers advertise each candidate.
- The project does not implement choking/unchoking or a free-rider policy in the inspected source.
- CSV logging is optional and is enabled for a peer process with `--log <file>`; this code does not automatically choose a per-run directory itself. The test script or caller supplies the desired path.

## Build-target map

The [`Makefile`](../../Makefile) creates three executables:

- `metainfo`: `src/metainfo.c` compiled with `-DMETAINFO_CLI`, plus hashing, protocol, and tracker utility code.
- `tracker`: `src/tracker.c` compiled with `-DTRACKER_MAIN`, plus protocol and SHA-256 code.
- `peer`: the peer coordinator and peer modules, plus metadata, protocol, tracker helpers, SHA-256, and logging.

Compile-time macros control which `main()` function is included in `metainfo.c` and `tracker.c`.
