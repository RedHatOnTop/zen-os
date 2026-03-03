#!/usr/bin/env bash
# Zen OS Testing Infrastructure — QMP (QEMU Machine Protocol) Library
# Provides functions to communicate with a running QEMU instance via its
# QMP Unix socket.  All functions are non-interactive.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

# ─── Low-level Transport ─────────────────────────────────────────────────────

# Send a raw QMP command and return the JSON response.
# Usage: qmp_raw_send <socket> <json-command>
qmp_raw_send() {
    local socket="$1"
    local cmd="$2"

    if [[ ! -S "$socket" ]]; then
        log_error "QMP socket does not exist: $socket"
        return 1
    fi

    # QMP negotiation + command in a single socat session.
    # 1) Read greeting, 2) Send qmp_capabilities, 3) Send actual command.
    {
        echo '{"execute":"qmp_capabilities"}'
        sleep 0.2
        echo "$cmd"
        sleep 0.3
    } | socat - "UNIX-CONNECT:${socket}" 2>/dev/null | tail -n1
}

# ─── High-level Commands ─────────────────────────────────────────────────────

# Query VM status. Returns: "running", "paused", "shutdown", etc.
qmp_query_status() {
    local socket="$1"
    local resp
    resp=$(qmp_raw_send "$socket" '{"execute":"query-status"}')
    echo "$resp" | jq -r '.return.status // "unknown"'
}

# Graceful shutdown via ACPI power button.
qmp_system_powerdown() {
    local socket="$1"
    qmp_raw_send "$socket" '{"execute":"system_powerdown"}' >/dev/null
}

# Hard stop.
qmp_quit() {
    local socket="$1"
    qmp_raw_send "$socket" '{"execute":"quit"}' >/dev/null 2>&1 || true
}

# Pause (suspend) the VM.
qmp_stop() {
    local socket="$1"
    qmp_raw_send "$socket" '{"execute":"stop"}' >/dev/null
}

# Resume a paused VM.
qmp_cont() {
    local socket="$1"
    qmp_raw_send "$socket" '{"execute":"cont"}' >/dev/null
}

# Capture the guest framebuffer to a PPM image file.
# Usage: qmp_screendump <socket> <output-path>
qmp_screendump() {
    local socket="$1"
    local output="$2"
    ensure_dir "$(dirname "$output")"
    local cmd
    cmd=$(printf '{"execute":"screendump","arguments":{"filename":"%s"}}' "$output")
    qmp_raw_send "$socket" "$cmd" >/dev/null
    if [[ -f "$output" ]]; then
        log_ok "Screenshot saved: $output"
    else
        log_error "Screenshot failed — file not created: $output"
        return 1
    fi
}

# Dump guest physical memory to a file.
# Usage: qmp_dump_memory <socket> <output-path> [protocol]
# protocol defaults to "file:" for a raw binary dump.
qmp_dump_memory() {
    local socket="$1"
    local output="$2"
    local protocol="${3:-file:${output}}"
    ensure_dir "$(dirname "$output")"
    local cmd
    cmd=$(printf '{"execute":"dump-guest-memory","arguments":{"paging":false,"protocol":"%s"}}' "$protocol")
    qmp_raw_send "$socket" "$cmd" >/dev/null
    log_ok "Memory dump initiated: $output"
}

# Hot-add a USB storage device (for external storage testing).
# Usage: qmp_add_usb_drive <socket> <image-path> <drive-id>
qmp_add_usb_drive() {
    local socket="$1"
    local image="$2"
    local drive_id="${3:-usb-test-drive}"
    local cmd
    cmd=$(printf '{"execute":"blockdev-add","arguments":{"driver":"qcow2","node-name":"%s","file":{"driver":"file","filename":"%s"}}}' \
        "$drive_id" "$image")
    qmp_raw_send "$socket" "$cmd" >/dev/null
    cmd=$(printf '{"execute":"device_add","arguments":{"driver":"usb-storage","bus":"usb-bus.0","drive":"%s","id":"%s-dev"}}' \
        "$drive_id" "$drive_id")
    qmp_raw_send "$socket" "$cmd" >/dev/null
    log_ok "USB drive added: $drive_id ($image)"
}

# Hot-remove a USB device.
qmp_remove_device() {
    local socket="$1"
    local device_id="$2"
    local cmd
    cmd=$(printf '{"execute":"device_del","arguments":{"id":"%s"}}' "$device_id")
    qmp_raw_send "$socket" "$cmd" >/dev/null
    log_ok "Device removed: $device_id"
}

# Send a key combination to the guest (e.g., for triggering keyboard shortcuts).
# Usage: qmp_send_key <socket> <key> (key names per QEMU QKeyCode)
qmp_send_key() {
    local socket="$1"
    shift
    local keys_json=""
    for k in "$@"; do
        [[ -n "$keys_json" ]] && keys_json+=","
        keys_json+="{\"type\":\"qcode\",\"data\":\"$k\"}"
    done
    local cmd
    cmd=$(printf '{"execute":"send-key","arguments":{"keys":[%s]}}' "$keys_json")
    qmp_raw_send "$socket" "$cmd" >/dev/null
}

# Change the virtual display resolution.
qmp_set_display() {
    local socket="$1"
    local width="$2"
    local height="$3"
    # This works with virtio-gpu / QXL. May require guest driver cooperation.
    log_info "Display change requested: ${width}x${height} (requires guest cooperation)"
    # QEMU doesn't have a direct QMP command for resolution; it's typically done
    # via xrandr inside the guest. This is handled by the scenarios module.
}
