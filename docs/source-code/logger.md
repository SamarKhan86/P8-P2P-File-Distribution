# `src/logger.c` — CSV event logger

## Purpose in one sentence

`logger.c` writes optional peer activity events to a CSV file so a transfer can be inspected after it happens.

## Why this file exists

Without a shared logger, networking code would have to repeat timestamp formatting, CSV quoting, column ordering, file flushing, and concurrency protection at every event site. This module keeps that repetitive work in one place. The actual event decisions still belong to `peer.c`, `peer_connections.c`, and `peer_transfer.c`.

## Where it fits

`logger.c` implements the public interface declared in [`include/logger.h`](../../include/logger.h). The logger object is stored in `peer_state_t`, which is defined in [`include/peer.h`](../../include/peer.h). The peer calls `logger_open()` only when `--log <file>` was provided; event calls are harmless when the logger file pointer is not open.

## Responsibilities and functions

### `static void csv_field(FILE *file, const char *text)`

Writes one quoted CSV field. It surrounds the value with double quotes and doubles any double quote already inside the value. Newline and carriage-return characters inside the value become spaces so they do not create an accidental extra row. A null pointer is treated as an empty string. This is a private helper because other modules should use `logger_event()` rather than construct CSV columns themselves.

### `int logger_open(logger_t *logger, const char *path, double start_time)`

- Clears the logger structure, then opens `path` in append mode (`"a"`).
- Returns `-1` if `fopen()` fails.
- Stores the monotonic start-time value used for elapsed-time calculations.
- Initializes the mutex used to stop threads from writing interleaved rows.
- Writes the CSV header if the file position is zero, then flushes it.
- Returns `0` on success.

The header is:

```text
timestamp,elapsed_seconds,peer_port,event,remote_ip,remote_port,piece_index,bytes,status,details
```

`logger_open()` does not create parent directories. The caller must ensure the directory exists. The code also does not check the return value of `pthread_mutex_init()` or header writes.

### `void logger_event(...)`

Accepts the local peer port, event name, remote endpoint, piece index, byte count, status, and details. If the logger pointer or file is absent, it returns immediately. Otherwise it:

1. Creates a human-readable local timestamp with `localtime_r()` and `strftime()`.
2. Gets a monotonic clock reading and calculates elapsed seconds relative to `start_time`.
3. Locks the logger mutex.
4. Writes the values in the fixed column order, quoting text fields through `csv_field()`.
5. Leaves `remote_port` blank when zero and `piece_index` blank when negative.
6. Flushes the stream and unlocks the mutex.

Elapsed time is intended for duration comparisons; the formatted timestamp is local wall-clock time and contains no explicit timezone. All fields are written as one CSV row, but individual event placement and meaning are decided at the call site.

### `void logger_close(logger_t *logger)`

Closes an open log stream, clears the file pointer, and destroys the mutex. It does nothing for a null logger or a logger without an open file. The application should call it after event-producing threads have stopped using the logger.

## Event call sites in the current code

The logger itself does **not** invent or validate event names. The current peer modules emit these names:

| Event | Emitted by | Meaning |
|---|---|---|
| `peer_connected` | `peer_connections.c:add_active_peer()` | A handshake was accepted and a connection entry was set up; the local bitfield was sent. |
| `peer_disconnected` | `peer_connections.c:peer_disconnect_locked()` | The connection was closed/reset. |
| `piece_requested` | `peer.c:schedule_requests()` | A request was sent for a selected piece. |
| `piece_received` | `peer_transfer.c:peer_worker()` | A `PIECE` message arrived, before the hash result is known. |
| `piece_verified` | `peer_transfer.c:peer_worker()` | Piece length and SHA-256 matched, and the bytes were written to the output file. |
| `piece_hash_failed` | `peer_transfer.c:peer_worker()` | The received piece's SHA-256 did not match metadata. |
| `piece_uploaded` | `peer_transfer.c:peer_worker()` | The `PIECE` message containing a requested piece was successfully sent. |
| `download_complete` | `peer.c:main()` | The peer observes that all pieces are verified. This event is also reached by a peer that starts with all pieces, not only a peer that downloaded them during this run. |

One nuance: the logger does not currently emit a distinct event for every possible error, and a write failure or malformed message may not have its own CSV event. `piece_received` does not mean the piece has already passed verification. `piece_uploaded` means the local send function returned success; it does not prove that the remote peer subsequently verified the bytes.

## Threading and data

`logger_t` contains `FILE *file`, a `pthread_mutex_t mutex`, and the monotonic `start_time` value. Peer worker threads may emit events concurrently, so the mutex protects the row-writing sequence. The logger should be opened before event logging begins and closed only after the peer is done using it.

## Example: tracing one piece

For piece `3`, a downloader's CSV could show `piece_requested` from remote peer port `17001`, then `piece_received`, then `piece_verified` for piece `3`. A seeder or other downloader may separately record `piece_uploaded` for piece `3` with the requesting peer's endpoint. Correlate logs by piece index and peer endpoints, not merely by line order across different files.

## Limitations

- Logging is optional: without `--log`, calls return without writing.
- This module appends to a given path; it does not select a unique run folder.
- The logger uses a mutex per logger object, not a cross-process lock. Each process should have its own log file.
- Text values are CSV-quoted; `bytes` and numeric columns are written as numbers.
- Event detail strings are plain descriptions, not a versioned structured schema.

## Related files

- [`include/logger.h`](../../include/logger.h) — data structure and public functions.
- [`peer.md`](peer.md) — `--log` parsing, initialization, and completion event.
- [`peer_connections.md`](peer_connections.md) — connection events.
- [`peer_transfer.md`](peer_transfer.md) — piece receipt, verification, failure, and upload events.
