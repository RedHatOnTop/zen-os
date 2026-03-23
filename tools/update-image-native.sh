#!/usr/bin/env bash
# Update zen-compositor binary in the test image using Linux-native /tmp
# to avoid WSL2 /mnt/c mtime/cache issues.
# Must be run as root (sudo).
set -euo pipefail

BIN="${1:-/mnt/c/Users/jin14/Projects/zen-os/builddir/src/compositor/zen-compositor}"
IMG="${2:-/mnt/c/Users/jin14/Projects/zen-os/.zen-test-vms/zen-test.qcow2}"

[[ $EUID -ne 0 ]] && { echo "ERROR: Must be root (run with sudo)"; exit 1; }
[[ ! -f "$BIN" ]] && { echo "ERROR: Binary not found: $BIN"; exit 1; }
[[ ! -f "$IMG" ]] && { echo "ERROR: Image not found: $IMG"; exit 1; }

WORK=$(mktemp -d /tmp/zen-native-XXXXXX)
BASE="$WORK/base.qcow2"
RAW="$WORK/disk.raw"
MNT="$WORK/mnt"
OUT="$WORK/updated.qcow2"

cleanup() {
    umount "$MNT" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "[1/5] Copying image to /tmp (Linux-native)..."
cp "$IMG" "$BASE"

echo "[2/5] Converting to raw..."
qemu-img convert -f qcow2 -O raw "$BASE" "$RAW"

echo "[3/5] Mounting partition..."
OFFSET=$(sfdisk -J "$RAW" | python3 -c \
    "import sys,json; d=json.load(sys.stdin); \
     print(d['partitiontable']['partitions'][0]['start'] * 512)")
echo "  Partition offset: $OFFSET bytes"
mkdir -p "$MNT"
mount -o loop,offset="$OFFSET" "$RAW" "$MNT"

echo "[4/5] Replacing binary..."
cp "$BIN" "$MNT/usr/local/bin/zen-compositor"
chmod +x "$MNT/usr/local/bin/zen-compositor"
# Also replace /usr/bin/zen-compositor which is what the systemd service uses
cp "$BIN" "$MNT/usr/bin/zen-compositor"
chmod +x "$MNT/usr/bin/zen-compositor"
echo "  /usr/local/bin: $(ls -lh $MNT/usr/local/bin/zen-compositor)"
echo "  /usr/bin:       $(ls -lh $MNT/usr/bin/zen-compositor)"
echo "  Key strings:"
strings "$MNT/usr/bin/zen-compositor" | grep -E 'Shell module|Zen OS Compositor initialized|Boot signal'
umount "$MNT"

echo "[5/5] Converting back to qcow2 and writing to Windows path..."
qemu-img convert -f raw -O qcow2 "$RAW" "$OUT"
cp "$OUT" "$IMG"

echo "Done: $IMG"
