#!/usr/bin/env bash
# Inspect the contents of the test image to find all compositor binaries and startup scripts.
set -euo pipefail

IMG="${1:-/home/jin14/zen-test.qcow2}"

WORK=$(mktemp -d /tmp/zen-inspect-XXXXXX)
RAW="$WORK/disk.raw"
MNT="$WORK/mnt"

cleanup() {
    sudo umount "$MNT" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

qemu-img convert -f qcow2 -O raw "$IMG" "$RAW"
OFFSET=$(sfdisk -J "$RAW" | python3 -c \
    "import sys,json; d=json.load(sys.stdin); \
     print(d['partitiontable']['partitions'][0]['start'] * 512)")
mkdir -p "$MNT"
sudo mount -o loop,offset="$OFFSET" "$RAW" "$MNT"

echo "=== All zen-compositor binaries ==="
sudo find "$MNT" -name 'zen-compositor' -exec ls -la {} \;

echo ""
echo "=== Systemd service files referencing zen-compositor ==="
sudo grep -rl 'zen-compositor' "$MNT/etc/systemd" 2>/dev/null | while read f; do
    echo "--- $f ---"
    sudo cat "$f"
done

echo ""
echo "=== rc.local ==="
sudo cat "$MNT/etc/rc.local" 2>/dev/null || echo "(no rc.local)"

echo ""
echo "=== /etc/profile.d/ scripts ==="
sudo ls "$MNT/etc/profile.d/" 2>/dev/null || echo "(empty)"

echo ""
echo "=== Autostart scripts ==="
sudo find "$MNT/etc" -name '*.sh' 2>/dev/null | head -10
