#!/bin/bash
# Debug script to inspect guest image for agent connectivity issues
set -euo pipefail

ROOTFS="/mnt/zen-verify"

echo "=== 1. Check virtio_console module exists in guest kernel ==="
KVER=$(ls "$ROOTFS/boot/" | grep vmlinuz- | head -1 | sed 's/vmlinuz-//')
echo "Kernel version: $KVER"
find "$ROOTFS/lib/modules/$KVER" -name '*virtio*console*' -o -name '*virtio_console*' 2>/dev/null || echo "  No virtio_console module found"
echo ""

echo "=== 2. Check modules-load.d config ==="
cat "$ROOTFS/etc/modules-load.d/virtio-serial.conf" 2>/dev/null || echo "  NOT FOUND"
echo ""

echo "=== 3. Check udev rule ==="
cat "$ROOTFS/etc/udev/rules.d/60-virtio-serial.rules" 2>/dev/null || echo "  NOT FOUND"
echo ""

echo "=== 4. Check agent binary ==="
ls -la "$ROOTFS/usr/local/bin/zen-test-agent" 2>/dev/null || echo "  NOT FOUND"
file "$ROOTFS/usr/local/bin/zen-test-agent" 2>/dev/null || true
echo ""

echo "=== 5. Check agent service file ==="
cat "$ROOTFS/etc/systemd/system/zen-test-agent.service" 2>/dev/null || echo "  NOT FOUND"
echo ""

echo "=== 6. Check service is enabled (symlinked) ==="
ls -la "$ROOTFS/etc/systemd/system/multi-user.target.wants/zen-test-agent.service" 2>/dev/null || echo "  NOT ENABLED"
echo ""

echo "=== 7. Check agent script first 10 lines ==="
head -10 "$ROOTFS/usr/local/bin/zen-test-agent" 2>/dev/null || true
echo ""

echo "=== 8. Check if jq is installed ==="
ls -la "$ROOTFS/usr/bin/jq" 2>/dev/null || echo "  jq NOT FOUND"
echo ""

echo "=== 9. Check if bash is at /bin/bash ==="
ls -la "$ROOTFS/bin/bash" 2>/dev/null || echo "  bash NOT FOUND at /bin/bash"
ls -la "$ROOTFS/usr/bin/bash" 2>/dev/null || echo "  bash NOT FOUND at /usr/bin/bash"
echo ""

echo "=== 10. Check initramfs for virtio_console ==="
# Check if virtio_console is built-in or module
grep -r "virtio_console" "$ROOTFS/lib/modules/$KVER/modules.builtin" 2>/dev/null && echo "  virtio_console is BUILT-IN" || echo "  virtio_console is NOT built-in"
grep "virtio_console" "$ROOTFS/lib/modules/$KVER/modules.dep" 2>/dev/null || echo "  virtio_console NOT in modules.dep"
echo ""

echo "=== 11. List all virtio modules ==="
find "$ROOTFS/lib/modules/$KVER" -name '*virtio*' 2>/dev/null | head -20
echo ""

echo "=== 12. Check if timeout command exists ==="
ls -la "$ROOTFS/usr/bin/timeout" 2>/dev/null || echo "  timeout NOT FOUND"
echo ""

echo "=== DONE ==="
