#!/usr/bin/env bash
set -euo pipefail
IMG="${1:-/mnt/c/Users/jin14/Projects/zen-os/.zen-test-vms/zen-test.qcow2}"
WORK=$(mktemp -d /tmp/zen-check-XXXXXX)
RAW="$WORK/disk.raw"
MNTPT="$WORK/rootfs"
cleanup() { umount "$MNTPT" 2>/dev/null || true; rm -rf "$WORK"; }
trap cleanup EXIT
qemu-img convert -f qcow2 -O raw "$IMG" "$RAW"
OFFSET=$(sfdisk -J "$RAW" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['partitiontable']['partitions'][0]['start'] * 512)")
mkdir -p "$MNTPT"
mount -o loop,offset="$OFFSET" "$RAW" "$MNTPT"
echo "Binary in image:"
ls -la "$MNTPT/usr/local/bin/zen-compositor"
echo "Key strings:"
strings "$MNTPT/usr/local/bin/zen-compositor" | grep -E 'Shell module|Zen OS Compositor initialized|Boot signal'
