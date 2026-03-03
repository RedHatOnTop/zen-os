#!/usr/bin/env bash
# Zen OS Testing Infrastructure — Configuration & Defaults
# All paths and tunables for zen-test-cli.

set -euo pipefail

# ─── Paths ────────────────────────────────────────────────────────────────────
ZEN_TEST_DIR="${ZEN_TEST_DIR:-/tmp/zen-test-vms}"
ZEN_LOG_DIR="${ZEN_LOG_DIR:-/tmp/zen-os-logs}"

# ─── QEMU Defaults ───────────────────────────────────────────────────────────
ZEN_DEFAULT_RAM=2048          # MB
ZEN_DEFAULT_CPUS=2
ZEN_DEFAULT_DISK="32G"
ZEN_DEFAULT_DISPLAY="1920x1080"
ZEN_QEMU_BIN="${ZEN_QEMU_BIN:-qemu-system-x86_64}"

# ─── OS Image ────────────────────────────────────────────────────────────────
# Path to the Zen OS disk image used for boot tests.
# Must be set before running start/create commands.
ZEN_OS_IMAGE="${ZEN_OS_IMAGE:-}"

# ─── Boot Detection ──────────────────────────────────────────────────────────
# Maximum seconds to wait for guest boot before declaring failure.
ZEN_BOOT_TIMEOUT="${ZEN_BOOT_TIMEOUT:-120}"
# The sentinel string the guest writes to serial on successful boot.
# If this is found in serial.log, boot is considered successful.
# Falls back to "login:" if guest does not emit a custom signal.
ZEN_BOOT_SIGNAL="${ZEN_BOOT_SIGNAL:-ZEN_BOOT_OK}"
ZEN_BOOT_FALLBACK_SIGNAL="login:"
# Poll interval (seconds) when waiting for boot.
ZEN_BOOT_POLL_INTERVAL="${ZEN_BOOT_POLL_INTERVAL:-2}"

# ─── Sanitizer ────────────────────────────────────────────────────────────────
# Patterns to scan for in serial.log that indicate a failure.
ZEN_ERROR_PATTERNS=(
    "ERROR: AddressSanitizer"
    "ERROR: LeakSanitizer"
    "ERROR: UndefinedBehaviorSanitizer"
    "Kernel panic"
    "BUG:"
    "Oops:"
    "segfault"
)

# ─── Helpers ──────────────────────────────────────────────────────────────────

# Ensure a directory exists. Idempotent.
ensure_dir() {
    local dir="$1"
    [[ -d "$dir" ]] || mkdir -p "$dir"
}

# Return the VM-specific directory: $ZEN_TEST_DIR/<vm-name>/
vm_dir() {
    local name="$1"
    echo "${ZEN_TEST_DIR}/${name}"
}

# Return the QMP socket path for a VM.
vm_qmp_socket() {
    local name="$1"
    echo "$(vm_dir "$name")/qmp.sock"
}

# Return the serial log path for a VM.
vm_serial_log() {
    local name="$1"
    echo "$(vm_dir "$name")/serial.log"
}

# Return the PID file path for a VM.
vm_pid_file() {
    local name="$1"
    echo "$(vm_dir "$name")/qemu.pid"
}

# Return the config file path for a VM.
vm_config_file() {
    local name="$1"
    echo "$(vm_dir "$name")/config.json"
}

# Logging helpers — all output goes to stderr so stdout stays clean for
# machine-readable results.
log_info()  { echo "[INFO]  $*" >&2; }
log_warn()  { echo "[WARN]  $*" >&2; }
log_error() { echo "[ERROR] $*" >&2; }
log_ok()    { echo "[OK]    $*" >&2; }

# Check if a required binary is available.
require_bin() {
    local bin="$1"
    if ! command -v "$bin" &>/dev/null; then
        log_error "Required binary not found: $bin"
        return 1
    fi
}
