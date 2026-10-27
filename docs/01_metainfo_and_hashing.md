# Feature 01: Metainfo File Format & SHA-256 Integrity Verification

## 1. Overview and Purpose

In a decentralized peer-to-peer network, files are transferred from untrusted peers across unreliable networks. To ensure data integrity, prevent data corruption, and identify individual pieces, the system divides binary files into fixed-size chunks (default: 16 KB) and computes cryptographic SHA-256 hashes for each chunk.

The metainfo module ([`src/metainfo.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/metainfo.c) and [`src/sha256.c`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/sha256.c)) handles:
1. Generating `.meta` files for arbitrary binary files.
2. Parsing and validating `.meta` files into in-memory structures.
3. Calculating piece sizes, piece counts, and remainder handling for the last piece.
4. Computing a unique 32-byte `file_id` (SHA-256 digest over the concatenated piece hashes).

---

## 2. Technical Architecture & File Layout

### Metainfo Structure (`metainfo_t` in [`include/metainfo.h`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/include/metainfo.h#L6-L13))
```c
typedef struct {
    char filename[256];
    uint64_t file_size;
    uint32_t piece_size;
    uint32_t piece_count;
    uint8_t file_id[SHA256_HASH_SIZE];
    uint8_t *piece_hashes; /* piece_count * SHA256_HASH_SIZE */
} metainfo_t;
```

### On-Disk Format (`<filename>.meta`)
```text
NAME <original_filename>
SIZE <total_file_bytes>
PIECE_SIZE <bytes_per_piece>
PIECES <total_piece_count>
FILE_ID <64_char_hex_hash>
HASHES
<64_char_hex_hash_piece_0>
<64_char_hex_hash_piece_1>
...
```

### Key Functions
- [`metainfo_create()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/metainfo.c#L17-L95): Reads input binary, calculates SHA-256 per piece, computes `file_id`, and writes formatted metadata.
- [`metainfo_load()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/metainfo.c#L97-L168): Parses metadata file, allocates `piece_hashes` array, and stores fields in `metainfo_t`.
- [`metainfo_piece_len()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/metainfo.c#L5-L15): Returns exact byte length for piece $i$, handling the last partial piece (`file_size % piece_size`).
- [`sha256_buffer()`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/src/sha256.c#L172-L181): Computes standard NIST SHA-256 digest (32 bytes) on a memory buffer.

---

## 3. Step-by-Step Flow

```text
[Input Binary File] 
        |
        v
+-------------------------------------------------------+
| Divide into 16 KB pieces                              |
| Piece 0 (16 KB) -> SHA-256 -> Hash 0 (32 bytes)      |
| Piece 1 (16 KB) -> SHA-256 -> Hash 1 (32 bytes)      |
| ...                                                   |
| Piece N (Rem)   -> SHA-256 -> Hash N (32 bytes)      |
+-------------------------------------------------------+
        |
        v
[Concatenated Hashes: Hash 0 || Hash 1 || ... || Hash N]
        |
        +---> SHA-256() ---> [32-byte FILE_ID] (Swarm ID)
        |
        v
[Write to <filename>.meta]
```

---

## 4. Viva Defense Questions & Answers

### Q1: Why divide files into fixed-size pieces rather than downloading the entire file as one stream?
**Answer:** 
Dividing the file into fixed-size pieces is the foundation of P2P file sharing:
1. **Parallelism:** Multiple pieces can be downloaded simultaneously from different peers.
2. **Pipelining & Sharing:** A downloader can begin uploading piece 0 to other peers immediately after receiving and verifying it, without waiting for the whole file to complete.
3. **Fault Tolerance & Error Recovery:** If data corruption occurs over TCP, only the single affected 16 KB piece needs to be re-downloaded, rather than restarting the entire multi-megabyte file download.

### Q2: What is SHA-256, and why do we hash per piece instead of hashing only the whole file?
**Answer:**
SHA-256 (Secure Hash Algorithm 256-bit) is a cryptographic hash function that produces a unique, fixed 32-byte (256-bit) digest. Hashing per piece enables **immediate verification upon piece arrival**. The peer runs SHA-256 on the received buffer; if the hash matches the metadata, it writes the piece to disk and broadcasts `HAVE` to the swarm. If a peer sends corrupted or malicious data, it is detected and discarded immediately before infecting other peers.

### Q3: How is the last piece handled if the file size is not an exact multiple of 16 KB?
**Answer:**
Handled via `metainfo_piece_len(meta, piece_idx)`:
```c
if (piece_idx == meta->piece_count - 1) {
    uint64_t remainder = meta->file_size % meta->piece_size;
    return remainder ? (uint32_t)remainder : meta->piece_size;
}
return meta->piece_size;
```
For example, for a 50,000-byte file with 16,384-byte piece size:
- Pieces 0, 1, 2 are 16,384 bytes.
- Piece 3 (the last piece) is $50,000 - (3 \times 16,384) = 848$ bytes.
The hash is computed over exactly 848 bytes.

### Q4: What is the `file_id` and what role does it play?
**Answer:**
The `file_id` is a 32-byte hash computed over all concatenated piece hashes. It acts as the globally unique identifier for that specific swarm and file version. During the protocol handshake, peers verify each other's `file_id`. If peer A is sharing file X and peer B is sharing file Y, the `file_id` mismatch triggers an immediate socket disconnect, preventing cross-swarm data corruption.

### Q5: How do you verify that your code's hash calculation is correct?
**Answer:**
In [`tests/test_multi_peer.sh`](file:///C:/Users/KIIT/Desktop/P8-week5-updated/p8-work/tests/test_multi_peer.sh#L28-L34), we extract the first 16,384 bytes of the binary and run Linux's standard `sha256sum`:
```bash
head -c 16384 source.bin | sha256sum
```
We compare this output directly against the first piece hash in `.meta`. They match character-for-character.
