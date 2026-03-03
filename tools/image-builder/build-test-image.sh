#!/usr/bin/env bash
# ╔══════════════════════════════════════════════════════════════════════════╗
# ║  build-test-image.sh — Zen OS Minimal Test Image Builder               ║
# ║  Creates a bootable Alpine Linux qcow2 with zen-compositor installed.  ║
# ╚══════════════════════════════════════════════════════════════════════════╝
#
# Usage:
#   sudo ./build-test-image.sh \
#       --compositor /path/to/zen-compositor \
#       --output /path/to/output.qcow2
#
# Prerequisites (host): qemu-img, sfdisk, mkfs.ext4, wget, mount, chroot

set -euo pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────
ALPINE_VERSION="3.20"
ALPINE_ARCH="x86_64"
ALPINE_MIRROR="https://dl-cdn.alpinelinux.org/alpine"
DISK_SIZE_MB=256
COMPOSITOR_BIN=""
OUTPUT=""
WORK_DIR=""

# ── Argument Parsing ─────────────────────────────────────────────────────────
usage() {
    cat <<EOF
build-test-image.sh — Create a minimal bootable qcow2 for QEMU testing

Usage:
  sudo $0 --compositor <path> --output <path> [options]

Options:
  --compositor <path>   Path to compiled zen-compositor binary (required)
  --output <path>       Path for output qcow2 image (required)
  --disk-size <MB>      Disk size in MB (default: 256)
  --alpine-ver <ver>    Alpine Linux version (default: 3.20)
  --help                Show this help

Prerequisites:
  qemu-img, sfdisk, mkfs.ext4, wget, mount, chroot (requires root)
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --compositor)  COMPOSITOR_BIN="$2";  shift 2 ;;
        --output)      OUTPUT="$2";          shift 2 ;;
        --disk-size)   DISK_SIZE_MB="$2";    shift 2 ;;
        --alpine-ver)  ALPINE_VERSION="$2";  shift 2 ;;
        --help|-h)     usage; exit 0 ;;
        *)             echo "Unknown option: $1"; usage; exit 1 ;;
    esac
done

if [[ -z "$COMPOSITOR_BIN" || -z "$OUTPUT" ]]; then
    echo "ERROR: --compositor and --output are required"
    usage
    exit 1
fi

if [[ ! -f "$COMPOSITOR_BIN" ]]; then
    echo "ERROR: Compositor binary not found: $COMPOSITOR_BIN"
    exit 1
fi

if [[ $EUID -ne 0 ]]; then
    echo "ERROR: This script must be run as root (for mount/chroot)"
    exit 1
fi

# ── Dependency Check ─────────────────────────────────────────────────────────
for cmd in qemu-img sfdisk mkfs.ext4 wget mount chroot; do
    if ! command -v "$cmd" &>/dev/null; then
        echo "ERROR: Missing required command: $cmd"
        exit 1
    fi
done

# ── Work Directory ───────────────────────────────────────────────────────────
WORK_DIR="$(mktemp -d /tmp/zen-image-XXXXXX)"
RAW_IMG="${WORK_DIR}/disk.raw"
ROOTFS="${WORK_DIR}/rootfs"
LOOP_DEV=""

cleanup() {
    echo "[CLEANUP] Cleaning up..."
    # Unmount everything
    umount -R "$ROOTFS" 2>/dev/null || true
    # Detach loop device
    if [[ -n "$LOOP_DEV" ]]; then
        losetup -d "$LOOP_DEV" 2>/dev/null || true
    fi
    rm -rf "$WORK_DIR"
    echo "[CLEANUP] Done"
}
trap cleanup EXIT

echo "============================================"
echo " Zen OS Test Image Builder"
echo " Alpine ${ALPINE_VERSION} | ${DISK_SIZE_MB}MB disk"
echo "============================================"

# ── Step 1: Download Alpine minirootfs ────────────────────────────────────────
ROOTFS_TAR="${WORK_DIR}/alpine-minirootfs.tar.gz"
ROOTFS_URL="${ALPINE_MIRROR}/v${ALPINE_VERSION}/releases/${ALPINE_ARCH}/alpine-minirootfs-${ALPINE_VERSION}.0-${ALPINE_ARCH}.tar.gz"

echo "[1/9] Downloading Alpine minirootfs..."
wget -q -O "$ROOTFS_TAR" "$ROOTFS_URL" || {
    echo "ERROR: Failed to download Alpine minirootfs from $ROOTFS_URL"
    exit 1
}
echo "     Downloaded: $(du -h "$ROOTFS_TAR" | cut -f1)"

# ── Step 2: Create raw disk image ────────────────────────────────────────────
echo "[2/9] Creating raw disk image (${DISK_SIZE_MB} MB)..."
dd if=/dev/zero of="$RAW_IMG" bs=1M count="$DISK_SIZE_MB" status=none

# Partition: 1 MiB BIOS boot gap + rest is ext4 root
echo "[3/9] Partitioning..."
sfdisk "$RAW_IMG" <<EOF >/dev/null 2>&1
label: dos
type=83, bootable
EOF

# ── Step 3: Setup loop device and format ─────────────────────────────────────
echo "[4/9] Setting up loop device and formatting..."
LOOP_DEV="$(losetup --find --show --partscan "$RAW_IMG")"
PART="${LOOP_DEV}p1"

# Wait for partition device to appear
for i in $(seq 1 10); do
    [[ -b "$PART" ]] && break
    sleep 0.5
done

if [[ ! -b "$PART" ]]; then
    echo "ERROR: Partition device $PART not found"
    exit 1
fi

mkfs.ext4 -q -L zenroot "$PART"

# ── Step 4: Mount and extract rootfs ─────────────────────────────────────────
echo "[5/9] Extracting Alpine rootfs..."
mkdir -p "$ROOTFS"
mount "$PART" "$ROOTFS"
tar xzf "$ROOTFS_TAR" -C "$ROOTFS"

# ── Step 5: Configure Alpine inside chroot ───────────────────────────────────
echo "[6/9] Configuring guest OS..."

# Setup resolv.conf for network inside chroot
cp /etc/resolv.conf "$ROOTFS/etc/resolv.conf"

# Mount pseudo-filesystems for chroot
mount -t proc proc "$ROOTFS/proc"
mount -t sysfs sysfs "$ROOTFS/sys"
mount --bind /dev "$ROOTFS/dev"
mount --bind /dev/pts "$ROOTFS/dev/pts"

# Setup apk repos
mkdir -p "$ROOTFS/etc/apk"
cat > "$ROOTFS/etc/apk/repositories" <<EOF
${ALPINE_MIRROR}/v${ALPINE_VERSION}/main
${ALPINE_MIRROR}/v${ALPINE_VERSION}/community
EOF

# Install packages via chroot
chroot "$ROOTFS" /bin/sh <<'CHROOTEOF'
set -e

apk update
apk add --no-cache \
    linux-virt \
    openrc \
    syslinux \
    mesa-dri-gallium \
    mesa-egl \
    mesa-gl \
    libinput \
    eudev \
    wayland \
    wayland-libs-server \
    xkeyboard-config \
    libxkbcommon

# Setup OpenRC
rc-update add devfs sysinit
rc-update add dmesg sysinit
rc-update add mdev sysinit
rc-update add hwdrivers sysinit

rc-update add networking boot
rc-update add hostname boot

# Create serial console service for kernel messages
sed -i 's/^#ttyS0/ttyS0/' /etc/inittab 2>/dev/null || true

# Set hostname
echo "zen-os-test" > /etc/hostname

# Set root password (empty for test)
echo "root:" | chpasswd

CHROOTEOF

# ── Step 6: Install compositor and auto-start script ─────────────────────────
echo "[7/9] Installing compositor and auto-start service..."

# Copy compositor binary
cp "$COMPOSITOR_BIN" "$ROOTFS/usr/bin/zen-compositor"
chmod +x "$ROOTFS/usr/bin/zen-compositor"

# Copy any required shared libs (ldd output) — best effort
if command -v ldd &>/dev/null; then
    ldd "$COMPOSITOR_BIN" 2>/dev/null | grep "=>" | awk '{print $3}' | while read lib; do
        if [[ -f "$lib" && ! -f "$ROOTFS$lib" ]]; then
            mkdir -p "$ROOTFS$(dirname "$lib")"
            cp "$lib" "$ROOTFS$lib" 2>/dev/null || true
        fi
    done
fi

# Create OpenRC init script for zen-compositor
cat > "$ROOTFS/etc/init.d/zen-compositor" <<'INITEOF'
#!/sbin/openrc-run

name="zen-compositor"
description="Zen OS Wayland Compositor"

command="/usr/bin/zen-compositor"
command_background=true
pidfile="/run/zen-compositor.pid"
output_log="/var/log/zen-compositor.log"
error_log="/var/log/zen-compositor.log"

depend() {
    need localmount
    after hwdrivers
}

start_pre() {
    # Required environment for headless Wayland compositor
    export WLR_BACKENDS=headless
    export WLR_RENDERER=gles2
    export XDG_RUNTIME_DIR=/run/user/0
    export WLR_HEADLESS_OUTPUTS=1

    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 0700 "$XDG_RUNTIME_DIR"
}
INITEOF
chmod +x "$ROOTFS/etc/init.d/zen-compositor"

# Add to default runlevel
chroot "$ROOTFS" rc-update add zen-compositor default

# ── Step 7: Install bootloader ───────────────────────────────────────────────
echo "[8/9] Installing bootloader..."

# Install syslinux MBR
dd if="$ROOTFS/usr/share/syslinux/mbr.bin" of="$LOOP_DEV" bs=440 count=1 conv=notrunc 2>/dev/null

# Install extlinux
mkdir -p "$ROOTFS/boot"
chroot "$ROOTFS" /bin/sh -c "extlinux --install /boot" 2>/dev/null || {
    # Fallback: write syslinux config manually
    echo "WARN: extlinux --install failed, writing config manually"
}

# Find kernel version
KERNEL_VER=$(ls "$ROOTFS/lib/modules/" | head -1)

# Write syslinux config
cat > "$ROOTFS/boot/extlinux.conf" <<EOF
DEFAULT zen
TIMEOUT 10
PROMPT 0

LABEL zen
    LINUX /boot/vmlinuz-virt
    INITRD /boot/initramfs-virt
    APPEND root=/dev/sda1 rootfstype=ext4 console=ttyS0 quiet
EOF

# ── Step 8: Unmount and convert ──────────────────────────────────────────────
echo "[9/9] Finalizing image..."

# Unmount pseudo-fs
umount -R "$ROOTFS/dev/pts" 2>/dev/null || true
umount -R "$ROOTFS/dev" 2>/dev/null || true
umount "$ROOTFS/sys" 2>/dev/null || true
umount "$ROOTFS/proc" 2>/dev/null || true
umount "$ROOTFS"

# Detach loop device
losetup -d "$LOOP_DEV"
LOOP_DEV=""

# Ensure output directory exists
mkdir -p "$(dirname "$OUTPUT")"

# Convert to qcow2
qemu-img convert -f raw -O qcow2 "$RAW_IMG" "$OUTPUT"

echo ""
echo "============================================"
echo " Image built successfully!"
echo " Output: $OUTPUT"
echo " Size:   $(du -h "$OUTPUT" | cut -f1)"
echo "============================================"
