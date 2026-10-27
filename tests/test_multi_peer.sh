#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

TMP="$(mktemp -d)"
RUN_ID="$(date +%Y%m%d-%H%M%S)-$$"
LOG_DIR="$ROOT/logs/runs/$RUN_ID"
mkdir -p "$LOG_DIR"
PIDS=()
cleanup() {
    for p in "${PIDS[@]:-}"; do
        kill "$p" 2>/dev/null || true
    done
    rm -rf "$TMP"
}
trap cleanup EXIT

echo "=== 1. Compiling application ==="
make all >/dev/null

echo "=== 2. Testing metainfo creation & SHA-256 verification ==="
python3 - "$TMP/source.bin" <<'PY'
import sys
with open(sys.argv[1], 'wb') as f:
    f.write(bytes((i * 101 + 37) % 256 for i in range(16384 * 4 + 1234)))
PY

./metainfo create "$TMP/source.bin" "$TMP/source.meta" 16384
./metainfo show "$TMP/source.meta"

PIECE0_HASH=$(head -c 16384 "$TMP/source.bin" | sha256sum | awk '{print $1}')
META_PIECE0=$(grep -A 2 'HASHES' "$TMP/source.meta" | tail -n 1)
if [ "$PIECE0_HASH" != "$META_PIECE0" ]; then
    echo "ERROR: Piece 0 hash mismatch! Expected $META_PIECE0, got $PIECE0_HASH"
    exit 1
fi
echo "OK: Piece 0 hash matches sha256sum ($PIECE0_HASH)"

echo "=== 3. Starting Central Tracker on port 16000 ==="
./tracker 16000 2>&1 | tee "$LOG_DIR/tracker.log" &
PIDS+=("$!")
sleep 0.4

echo "=== 4. Starting Initial Seeder on port 17001 ==="
./peer "$TMP/source.meta" "$TMP/source.bin" 17001 127.0.0.1 16000 --seed --seed-time 1 --log "$LOG_DIR/peer_17001.csv" 2>&1 | tee "$LOG_DIR/seeder.log" &
PIDS+=("$!")
sleep 0.5

echo "=== 5. Starting Downloader 1 on port 17002 ==="
./peer "$TMP/source.meta" "$TMP/out-1.bin" 17002 127.0.0.1 16000 --seed-time 3 --log "$LOG_DIR/peer_17002.csv" 2>&1 | tee "$LOG_DIR/dl1.log" &
DL1_PID=$!

echo "=== 6. Letting Downloader 1 become a temporary seeder ==="
sleep 2

echo "=== 7. Starting Downloader 2 on port 17003 ==="
./peer "$TMP/source.meta" "$TMP/out-2.bin" 17003 127.0.0.1 16000 --exit-on-complete --log "$LOG_DIR/peer_17003.csv" 2>&1 | tee "$LOG_DIR/dl2.log" &
DL2_PID=$!

echo "=== 8. Waiting for swarm transfer to complete ==="
wait "$DL1_PID"
wait "$DL2_PID"

echo "=== 9. Verifying byte-for-byte data integrity ==="
cmp "$TMP/source.bin" "$TMP/out-1.bin"
echo "OK: out-1.bin verified byte-for-byte with source.bin"

cmp "$TMP/source.bin" "$TMP/out-2.bin"
echo "OK: out-2.bin verified byte-for-byte with source.bin"

echo "=== 10. Verifying persistent event logs ==="
grep -q '^timestamp,elapsed_seconds,peer_port,event,' "$LOG_DIR/peer_17002.csv"
grep -q 'piece_verified' "$LOG_DIR/peer_17002.csv"
grep -q 'download_complete' "$LOG_DIR/peer_17002.csv"
grep -q 'piece_uploaded' "$LOG_DIR/peer_17002.csv"
grep -q '^timestamp,elapsed_seconds,peer_port,event,' "$LOG_DIR/peer_17003.csv"
grep -q 'download_complete' "$LOG_DIR/peer_17003.csv"
echo "OK: persistent event logs kept in $LOG_DIR"

echo "PASS: multi-peer distribution (seeder and 2 downloaders), byte-for-byte verification, and CSV performance logs."
