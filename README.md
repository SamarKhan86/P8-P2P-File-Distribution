# P2P File Distribution — BitTorrent-lite

Prototype implementation for **CS-30003 Coding Assignment 1 (Project P8)**.

This repository is a **course-assignment prototype** and is being developed incrementally. It currently demonstrates core building blocks (metainfo hashing, a simple tracker registry, and a localhost TCP handshake), not a complete BitTorrent-compatible client.

## Overview

This project explores how a BitTorrent-like peer-to-peer system can be built in C using sockets and piece-based transfer ideas:
- a tracker helps peers discover each other,
- files are split into pieces,
- each piece is verified with cryptographic hashes,
- peers exchange protocol messages over TCP.

The original proposal also targets multi-peer transfer strategies such as rarest-first selection, request pipelining, and choking/unchoking. Those parts are planned/in progress (see below).

## Current status (implemented prototype pieces)

What is currently present in this repository:

- **Metainfo generator** (`src/metainfo.c`, `src/sha256.c`)
  - Reads a file and creates `<filename>.meta`
  - Uses **1024-byte piece size**
  - Computes **SHA-256** hash for each piece
- **Tracker prototype** (`src/tracker.c`)
  - Interactive in-memory registry
  - Register / show / remove peers
  - Capacity limit: **10 peers**
- **TCP handshake demo** (`src/tcp_client.c`, `src/tcp_server.c`)
  - Localhost handshake messages like `HANDSHAKE | Peer_ID: ...`
  - Demonstrates connection + message exchange on `127.0.0.1:6034`
- **Peer interface definitions** (`src/peer.h`)
  - Structures for peer connections/states and pthread-oriented manager APIs

## Planned functionality (in progress / not complete yet)

The following are design goals from the project proposal and docs, but should be treated as planned work unless explicitly implemented in code:

- Full multi-peer file distribution and piece exchange
- Piece availability coordination (`HAVE`, `REQUEST`, `PIECE`, etc.)
- Rarest-first piece selection experiments
- Request pipelining
- Choking/unchoking policy
- End-to-end file reconstruction and larger-scale concurrency evaluation

## Architecture / components

Current code and design docs are organized around these components:

- **Tracker**: peer registration and discovery prototype
- **Peer communication**: TCP-based handshake/message exchange prototype
- **Metainfo manager**: file metadata + per-piece SHA-256 hashes
- **Peer manager/state model**: interfaces in `peer.h` for connection/state handling

## Repository layout

```text
.
├── README.md
├── AI-USE.md
├── docs/
│   ├── component_interfaces.md
│   ├── metainfo.md
│   └── peer_protocol.md
└── src/
    ├── metainfo.c
    ├── peer.h
    ├── sha256.c
    ├── sha256.h
    ├── tcp_client.c
    ├── tcp_server.c
    └── tracker.c
```

## Protocol and metainfo references

- [Peer protocol notes](docs/peer_protocol.md)
- [Metainfo format](docs/metainfo.md)
- [Component interface sketch](docs/component_interfaces.md)

## Build instructions

No Makefile is currently included, so compile the standalone programs directly:

```bash
gcc -Wall -Wextra -o tcp_server src/tcp_server.c
gcc -Wall -Wextra -o tcp_client src/tcp_client.c
gcc -Wall -Wextra -o tracker src/tracker.c
gcc -Wall -Wextra -o metainfo src/metainfo.c src/sha256.c
```

> Note: Build structure/flags may evolve as the project is completed.

## Localhost handshake demo

Safety note: the current TCP demo is intended for **local testing only** and uses `127.0.0.1:6034`.

In two terminals:

1. Start the listener process (from `tcp_client.c`):
   ```bash
   ./tcp_client
   ```
2. Start the connector process (from `tcp_server.c`):
   ```bash
   ./tcp_server
   ```

Expected behavior: each side prints sent/received `HANDSHAKE | Peer_ID: ...` messages, then exits.

## Metainfo generation example

Create a sample file and run the metainfo tool:

```bash
echo "hello p2p" > sample.txt
./metainfo
```

When prompted, enter:

```text
sample.txt
```

This produces `sample.txt.meta` in the current directory with file size, piece size, piece count, and per-piece SHA-256 hashes.

## Limitations and next steps

Current limitations:
- No persistent tracker storage
- No full peer swarm orchestration
- No complete piece download/upload workflow yet
- No completed strategy benchmarking yet

Next steps:
- Connect tracker + peer protocol into end-to-end transfer
- Implement and validate piece scheduling/pipelining/choking logic
- Add reproducible experiment scripts and evaluation results

## Team

Project team (as stated in the proposal):
- Suyog Lamsal
- Satyam Yadav
- Subash Gaihre
- Md Samar Khan
