# P2P BitTorrent Lite

An educational POSIX/C implementation that distributes one binary file as SHA-256 verified pieces between localhost TCP peers. It is a simplified custom protocol, not the official BitTorrent protocol.

## How to run and demonstrate the project

Use Ubuntu/WSL with GCC, Make, Bash, Python 3, `sha256sum`, and `cmp`.

### Option 1: Run via WSL terminal — automated demonstration

1. Open **Ubuntu/WSL**. From PowerShell or Command Prompt, type:
   
   ```bash
   wsl
   ```

2. Go to this project folder:
   
   ```bash
   cd /mnt/c/Users/KIIT/Desktop/P2P-Bit_Torrent_Lite
   ```

3. Build and run the complete demonstration:
   
   ```bash
   make clean && make all && make test
   ```
   
   The test creates a binary sample file and its metadata, starts a tracker,
   an initial seeder, and two downloaders. It verifies both reconstructed files
   using `cmp`, then confirms that downloader `17002` uploaded pieces to
   downloader `17003` after the original seeder exits.

4. Inspect the retained evidence:
   
   ```bash
   ls logs/runs
   ls logs/runs/<run_id>
   grep piece_uploaded logs/runs/<run_id>/peer_17002.csv
   ```
   
   Replace `<run_id>` with the directory printed by the test. `piece_uploaded`
   rows prove that a downloader shared data with another downloader.

### Option 2: Step-by-step, separate terminals

Open four Ubuntu/WSL terminal tabs. In **every** terminal first run:

```bash
cd /mnt/c/Users/KIIT/Desktop/P2P-Bit_Torrent_Lite
```

Before the demo, build once in any terminal:

```bash
make all
mkdir -p logs/runs/viva-demo
```

#### Terminal 1 — Start the tracker

```bash
./tracker 16000
```

The tracker registers peers and returns addresses. It does not transfer file
pieces.

#### Terminal 2 — Create the file and start the initial seeder

```bash
python3 -c "open('sample.bin', 'wb').write(b'Educational P2P Lite Transfer ' * 2000)"
./metainfo create sample.bin sample.meta 16384
./peer sample.meta sample.bin 17001 127.0.0.1 16000 --seed --seed-time 2 --log logs/runs/viva-demo/peer_17001.csv
```

#### Terminal 3 — Start downloader 1

```bash
./peer sample.meta downloaded_1.bin 17002 127.0.0.1 16000 --seed-time 10 --log logs/runs/viva-demo/peer_17002.csv
```

`--seed-time 10` keeps this downloader alive briefly after completion so it can
upload verified pieces to another peer.

Wait about three seconds after starting downloader 1. This lets the original
seeder exit and leaves downloader 1 as a verified temporary seeder.

#### Terminal 4 — Start downloader 2

```bash
./peer sample.meta downloaded_2.bin 17003 127.0.0.1 16000 --exit-on-complete --log logs/runs/viva-demo/peer_17003.csv
```

Watch the live terminal messages: connections, piece downloads, SHA-256
verification, uploads, and completion remain visible while CSV logs are also
written.

#### Verify reconstructed files

After both downloaders finish, run in any terminal:

```bash
cmp sample.bin downloaded_1.bin
cmp sample.bin downloaded_2.bin
```

No output from `cmp` means the files are byte-for-byte identical.

#### Inspect the CSV evidence

```bash
grep -E 'piece_requested|piece_received|piece_verified' logs/runs/viva-demo/peer_17002.csv
grep piece_uploaded logs/runs/viva-demo/peer_17002.csv
```

The first command shows the download lifecycle. The second shows successful
uploads. See [logs/README.md](logs/README.md) for the CSV columns and Excel
instructions.

A peer may also use `--peer ip:port`, `--tracker ip:port`,
`--exit-on-complete`, or `--seed-time seconds`.

## Implemented features

- Tracker-based discovery; direct peer-to-peer pieces
- Handshake, bitfield, HAVE, request and piece messages
- SHA-256 verification before storage/sharing
- Rarest-first selection and four-request pipeline per connection
- Listener plus per-connection worker threads and mutex-protected state
- Per-peer optional CSV logs

## Simplifications

Localhost/single-file scope, fixed peer limit, no official BitTorrent compatibility, DHT, NAT traversal, authentication, encryption, choking, free-rider policy, or persistent tracker database. `CANCEL` is defined but not acted upon.

Read [Design.md](Design.md), the [source-code guide](docs/source-code/README.md), [log guide](logs/README.md), and [viva guide](docs/VIVA_QUESTIONS.md).
