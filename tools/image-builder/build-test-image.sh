#!/usr/bin/env bash
# =====================================================================
#  build-test-image.sh -- Zen OS Minimal Test Image Builder
#  Creates a bootable Ubuntu 24.04 qcow2 with zen-compositor installed.
# =====================================================================
set -euo pipefail

UBUNTU_SUITE="noble"
UBUNTU_MIRROR="http://archive.ubuntu.com/ubuntu"
DISK_SIZE_MB=2048
COMPOSITOR_BIN=""
BUILD_DIR=""
OUTPUT=""
WORK_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --compositor)  COMPOSITOR_BIN="$2";  shift 2 ;;
        --builddir)    BUILD_DIR="$2";       shift 2 ;;
        --output)      OUTPUT="$2";          shift 2 ;;
        --disk-size)   DISK_SIZE_MB="$2";    shift 2 ;;
        --help|-h)     echo "Usage: sudo $0 --compositor PATH --builddir PATH --output PATH"; exit 0 ;;
        *)             echo "Unknown: $1"; exit 1 ;;
    esac
done

[[ -z "$COMPOSITOR_BIN" || -z "$OUTPUT" ]] && { echo "ERROR: --compositor and --output required"; exit 1; }
[[ ! -f "$COMPOSITOR_BIN" ]] && { echo "ERROR: Binary not found: $COMPOSITOR_BIN"; exit 1; }
[[ $EUID -ne 0 ]] && { echo "ERROR: Must be root"; exit 1; }
[[ -z "$BUILD_DIR" ]] && BUILD_DIR="$(dirname "$(dirname "$(dirname "$COMPOSITOR_BIN")")")"

for cmd in qemu-img sfdisk mkfs.ext4 debootstrap mount chroot; do
    command -v "$cmd" &>/dev/null || { echo "ERROR: Missing: $cmd"; exit 1; }
done

WORK_DIR="$(mktemp -d /tmp/zen-image-XXXXXX)"
RAW_IMG="${WORK_DIR}/disk.raw"
ROOTFS="${WORK_DIR}/rootfs"
LOOP_DEV=""

cleanup() {
    echo "[CLEANUP] Tearing down..."
    umount "$ROOTFS/dev/pts"  2>/dev/null || true
    umount "$ROOTFS/dev"      2>/dev/null || true
    umount "$ROOTFS/sys"      2>/dev/null || true
    umount "$ROOTFS/proc"     2>/dev/null || true
    umount "$ROOTFS"          2>/dev/null || true
    [[ -n "$LOOP_DEV" ]] && losetup -d "$LOOP_DEV" 2>/dev/null || true
    rm -rf "$WORK_DIR"
    echo "[CLEANUP] Done"
}
trap cleanup EXIT

echo "[1/8] Creating raw disk image (${DISK_SIZE_MB} MB)..."
dd if=/dev/zero of="$RAW_IMG" bs=1M count="$DISK_SIZE_MB" status=none

echo "[2/8] Partitioning..."
sfdisk "$RAW_IMG" <<EOF >/dev/null 2>&1
label: dos
type=83, bootable
EOF

echo "[3/8] Setting up loop device..."
LOOP_DEV="$(losetup --find --show --partscan "$RAW_IMG")"
PART="${LOOP_DEV}p1"
for _ in $(seq 1 20); do [[ -b "$PART" ]] && break; sleep 0.3; done
[[ ! -b "$PART" ]] && { echo "ERROR: Partition $PART not found"; exit 1; }
mkfs.ext4 -q -L zenroot "$PART"

echo "[4/8] Bootstrapping Ubuntu ${UBUNTU_SUITE}..."
mkdir -p "$ROOTFS"
mount "$PART" "$ROOTFS"
debootstrap --variant=minbase \
    --include=systemd,systemd-sysv,udev,dbus,kmod,init \
    "$UBUNTU_SUITE" "$ROOTFS" "$UBUNTU_MIRROR"

echo "[5/8] Configuring guest OS..."
mount -t proc proc "$ROOTFS/proc"
mount -t sysfs sysfs "$ROOTFS/sys"
mount --bind /dev "$ROOTFS/dev"
mount --bind /dev/pts "$ROOTFS/dev/pts"
cp /etc/resolv.conf "$ROOTFS/etc/resolv.conf"
echo "zen-os-test" > "$ROOTFS/etc/hostname"
echo "/dev/vda1  /  ext4  errors=remount-ro  0  1" > "$ROOTFS/etc/fstab"

chroot "$ROOTFS" /bin/bash <<'CHROOTEOF'
set -e
export DEBIAN_FRONTEND=noninteractive
cat > /etc/apt/sources.list <<APT
deb http://archive.ubuntu.com/ubuntu noble main universe
deb http://archive.ubuntu.com/ubuntu noble-updates main universe
APT
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    linux-image-virtual grub-pc \
    libdrm2 libegl1 libgbm1 libgles2 libgl1-mesa-dri \
    libinput10 libwayland-server0 libwayland-client0 \
    libxkbcommon0 libpixman-1-0 libseat1 libudev1 \
    libdisplay-info1 libliftoff0 libevdev2 libmtdev1t64 \
    libgudev-1.0-0 libffi8 \
    libxcb1 libxcb-composite0 libxcb-dri3-0 libxcb-present0 \
    libxcb-render0 libxcb-render-util0 libxcb-shm0 \
    libxcb-xfixes0 libxcb-xinput0 libxcb-ewmh2 libxcb-icccm4 libxcb-res0
systemctl enable serial-getty@ttyS0.service
passwd -d root
apt-get clean
rm -rf /var/lib/apt/lists/*
CHROOTEOF

echo "[6/8] Installing compositor and libraries..."
install -m 755 "$COMPOSITOR_BIN" "$ROOTFS/usr/bin/zen-compositor"
ZEN_LIB_DIR="$ROOTFS/usr/lib/zen"
mkdir -p "$ZEN_LIB_DIR"

SUBPROJECT_LIBS=(
    "subprojects/scenefx/libscenefx-0.4.so"
    "subprojects/wlroots/libwlroots-0.19.so"
    "subprojects/wayland-1.24.0/src/libwayland-server.so.0"
    "subprojects/wayland-1.24.0/src/libwayland-client.so.0"
    "subprojects/pixman/pixman/libpixman-1.so.0"
)
for lib in "${SUBPROJECT_LIBS[@]}"; do
    src="${BUILD_DIR}/${lib}"
    if [[ -f "$src" ]]; then
        base="$(basename "$src")"
        cp "$src" "$ZEN_LIB_DIR/$base"
        echo "  copied: $base"
        soname="$(readelf -d "$src" 2>/dev/null | grep SONAME | sed 's/.*\[\(.*\)\]/\1/' || true)"
        if [[ -n "$soname" && "$soname" != "$base" ]]; then
            ln -sf "$base" "$ZEN_LIB_DIR/$soname"
            echo "  symlink: $soname -> $base"
        fi
    else
        echo "  WARNING: not found: $src"
    fi
done
echo "/usr/lib/zen" > "$ROOTFS/etc/ld.so.conf.d/zen.conf"
chroot "$ROOTFS" ldconfig

cat > "$ROOTFS/etc/systemd/system/zen-compositor.service" <<'UNITEOF'
[Unit]
Description=Zen OS Wayland Compositor
After=systemd-logind.service
Wants=systemd-logind.service

[Service]
Type=simple
Environment=WLR_BACKENDS=headless
Environment=WLR_RENDERER=gles2
Environment=WLR_RENDERER_ALLOW_SOFTWARE=1
Environment=WLR_HEADLESS_OUTPUTS=1
Environment=XDG_RUNTIME_DIR=/run/user/0
Environment=WLR_LIBINPUT_NO_DEVICES=1
Environment=LD_LIBRARY_PATH=/usr/lib/zen
ExecStartPre=/bin/mkdir -p /run/user/0
ExecStartPre=/bin/chmod 0700 /run/user/0
ExecStart=/usr/bin/zen-compositor
StandardOutput=journal+console
StandardError=journal+console
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
UNITEOF
chroot "$ROOTFS" systemctl enable zen-compositor.service

echo "[7/8] Installing GRUB..."
chroot "$ROOTFS" grub-install --target=i386-pc --boot-directory=/boot "$LOOP_DEV"
KERNEL_VER=$(ls "$ROOTFS/boot/" | grep "vmlinuz-" | head -1 | sed 's/vmlinuz-//')
mkdir -p "$ROOTFS/boot/grub"
cat > "$ROOTFS/boot/grub/grub.cfg" <<GRUBEOF
set timeout=1
set default=0
menuentry "Zen OS Test" {
    linux /boot/vmlinuz-${KERNEL_VER} root=/dev/vda1 ro console=ttyS0 quiet
    initrd /boot/initrd.img-${KERNEL_VER}
}
GRUBEOF

echo "[8/8] Finalizing image..."
umount "$ROOTFS/dev/pts" 2>/dev/null || true
umount "$ROOTFS/dev"     2>/dev/null || true
umount "$ROOTFS/sys"     2>/dev/null || true
umount "$ROOTFS/proc"    2>/dev/null || true
sync
umount "$ROOTFS"
losetup -d "$LOOP_DEV"
LOOP_DEV=""
mkdir -p "$(dirname "$OUTPUT")"
qemu-img convert -f raw -O qcow2 "$RAW_IMG" "$OUTPUT"
echo ""
echo "============================================"
echo " Image built successfully!"
echo " Output: $OUTPUT"
echo " Size:   $(du -h "$OUTPUT" | cut -f1)"
echo "============================================"
