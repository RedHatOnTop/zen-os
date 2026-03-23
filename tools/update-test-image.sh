#!/usr/bin/env bash
# Update the zen-compositor binary inside the test qcow2 image.
# Must be run as root (or with sudo).
set -euo pipefail

IMG="${1:-/mnt/c/Users/jin14/Projects/zen-os/.zen-test-vms/zen-test.qcow2}"
BIN="${2:-/mnt/c/Users/jin14/Projects/zen-os/builddir/src/compositor/zen-compositor}"

[[ $EUID -ne 0 ]] && { echo "ERROR: Must be root (run with sudo)"; exit 1; }
[[ ! -f "$IMG" ]] && { echo "ERROR: Image not found: $IMG"; exit 1; }
[[ ! -f "$BIN" ]] && { echo "ERROR: Binary not found: $BIN"; exit 1; }

WORK=$(mktemp -d /tmp/zen-update-XXXXXX)
RAW="$WORK/disk.raw"
MNTPT="$WORK/rootfs"

cleanup() {
    umount "$MNTPT" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "[1/4] Converting qcow2 to raw..."
qemu-img convert -f qcow2 -O raw "$IMG" "$RAW"

echo "[2/4] Mounting partition..."
OFFSET=$(sfdisk -J "$RAW" | python3 -c \
    "import sys,json; d=json.load(sys.stdin); \
     print(d['partitiontable']['partitions'][0]['start'] * 512)")
echo "  Partition offset: $OFFSET bytes"
mkdir -p "$MNTPT"
mount -o loop,offset="$OFFSET" "$RAW" "$MNTPT"

echo "[3/4] Replacing compositor binary..."
cp "$BIN" "$MNTPT/usr/local/bin/zen-compositor"
chmod +x "$MNTPT/usr/local/bin/zen-compositor"
echo "  Done: $(ls -lh $MNTPT/usr/local/bin/zen-compositor)"

umount "$MNTPT"

echo "[4/4] Converting back to qcow2..."
qemu-img convert -f raw -O qcow2 "$RAW" "$IMG"

echo "Image updated: $IMG"
