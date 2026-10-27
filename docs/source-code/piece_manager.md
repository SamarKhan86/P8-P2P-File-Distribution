# `src/piece_manager.c` — piece length, reads, and resume scanning

## Purpose in one sentence

`piece_manager.c` provides small helpers for determining a piece's length, reading it from disk, and recognizing already-verified pieces in an existing output file.

## Why this file exists

Piece offsets and final-piece lengths are easy to get wrong if duplicated in upload and resume logic. This module gives the rest of the peer code a small place to perform these file-piece operations consistently. In this version it is intentionally small; the actual receive-side SHA-256 verification and `pwrite()` operation still live in `peer_transfer.c`.

## Where it fits

The public declarations are in [`include/piece_manager.h`](../../include/piece_manager.h). `peer.c:main()` calls `piece_manager_scan_existing()` during startup. `peer_transfer.c` calls `piece_manager_read()` when another peer requests a piece. `metainfo_piece_len()` comes from `metainfo.c`, and `sha256_buffer()` comes from `sha256.c`.

## Functions

### `uint32_t piece_manager_length(const peer_state_t *state, uint32_t piece_idx)`

Returns `metainfo_piece_len(&state->meta, piece_idx)`. This is a thin wrapper rather than a second implementation of final-piece length logic. It returns zero for an out-of-range piece index via `metainfo_piece_len()`.

### `int piece_manager_read(const peer_state_t *state, uint32_t piece_idx, uint8_t **data, uint32_t *length)`

1. Sets `*length` to the valid length for the piece.
2. Allocates a buffer of that length and stores the pointer in `*data`.
3. Reads from `state->file_fd` using `pread()` at offset `piece_idx * piece_size`.
4. Requires the number of bytes read to equal the expected length.
5. On allocation or read failure, returns `-1`; on read failure it frees the buffer and sets `*data` to null. On success it returns `0` and the caller must free the buffer.

The caller is expected to supply valid non-null output pointers and a valid piece index. The function does not itself verify the read bytes' hash; the verified-piece bitfield is used by the caller before serving an upload.

### `void piece_manager_scan_existing(peer_state_t *state)`

Supports resuming from a prior partially downloaded output file. It checks the existing file size with `fstat()` and returns if the file is empty or the stat call fails. It allocates a buffer up to the configured piece size, then tries to read each piece at its fixed offset. For each full-length read, it calculates SHA-256 and compares the digest with the matching metadata hash. On a match, it sets the local bitfield, marks the piece `PIECE_VERIFIED`, and increments `verified_count`.

It only trusts bytes that can be read at the expected length and whose hash matches. It does not infer verification merely from the file's size. A failed allocation causes the scan to return without identifying reusable pieces.

## Why `pread()` is useful

`pread()` reads from a given file offset without changing a shared file-position cursor. That is helpful when workers may read pieces independently or pieces are stored in non-sequential positions.

## Example walkthrough

Suppose `download.bin` contains a valid first piece from a previous run but the rest is missing. At startup, `piece_manager_scan_existing()` reads piece zero, computes its SHA-256 hash, and compares it with metadata. If the digest matches, the local bitfield marks piece zero as available and `verified_count` increases. The scheduler only requests pieces still missing. When another peer requests piece zero, `piece_manager_read()` can load those bytes for uploading.

## Current boundary and limitations

The header comment says this is the home for piece-only work, but the current file contains only these three helpers. Receive-side hashing, file writes, piece-status updates, and `HAVE` broadcasting are still implemented in `peer_transfer.c`; do not tell a viva examiner that `piece_manager.c` owns all piece management.

`piece_manager_read()` does not guard against a zero length before allocation and assumes a valid piece index. In current upload flow, the caller checks the requested index and checks the local bitfield first.

## Likely viva questions

**Q: Why does the scan hash a piece instead of trusting the existing file?**

A: A previous file could be incomplete or corrupted. The hash comparison establishes that the bytes match the metadata's expected piece content.

**Q: Why is the final piece handled separately?**

A: The total file size may not be evenly divisible by the standard piece size.

**Q: Does this module download pieces from the network?**

A: No. It has disk helpers. Network message handling is in `peer_transfer.c`.

## Where to look next

- [`metainfo.md`](metainfo.md) — expected piece lengths and hashes.
- [`peer_transfer.md`](peer_transfer.md) — network upload and receive-side verification.
- [`peer.md`](peer.md) — startup and resume-scan call.
