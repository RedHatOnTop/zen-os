#!/usr/bin/env bash
# Zen OS Testing Infrastructure — VM Lifecycle Management
# Create, start, stop, pause, resume, destroy QEMU virtual machines.
# All operations are non-interactive and return immediately.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"
# shellcheck source=qmp.sh
source "${SCRIPT_DIR}/qmp.sh"
# shellcheck source=boot.sh
source "${SCRIPT_DIR}/boot.sh"

# ─── Create ───────────────────────────────────────────────────────────────────

# Create a new VM configuration.
# Usage: vm_create <name> [--ram MB] [--cpus N] [--disk SIZE] [--display WxH] [--image PATH]
vm_create() {
    local name="$1"
    shift
    local ram="$ZEN_DEFAULT_RAM"
    local cpus="$ZEN_DEFAULT_CPUS"
    local disk="$ZEN_DEFAULT_DISK"
    local display="$ZEN_DEFAULT_DISPLAY"
    local image="$ZEN_OS_IMAGE"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --ram)     ram="$2";     shift 2 ;;
            --cpus)    cpus="$2";    shift 2 ;;
            --disk)    disk="$2";    shift 2 ;;
            --display) display="$2"; shift 2 ;;
            --image)   image="$2";   shift 2 ;;
            *)         log_warn "Unknown option: $1"; shift ;;
        esac
    done

    local dir
    dir="$(vm_dir "$name")"

    if [[ -d "$dir" ]]; then
        log_error "VM already exists: $name (at $dir)"
        return 1
    fi

    ensure_dir "$dir"

    # Create a qcow2 overlay disk if no image provided, or create a backing
    # overlay on top of the provided base image.
    local disk_path="${dir}/disk.qcow2"
    if [[ -n "$image" && -f "$image" ]]; then
        qemu-img create -f qcow2 -b "$image" -F qcow2 "$disk_path" "$disk" 2>/dev/null
        log_info "Created overlay disk backed by: $image"
    else
        qemu-img create -f qcow2 "$disk_path" "$disk" 2>/dev/null
        log_info "Created blank disk: $disk_path ($disk)"
    fi

    # Write VM config as JSON for later reference.
    cat > "$(vm_config_file "$name")" <<EOF
{
  "name": "$name",
  "ram_mb": $ram,
  "cpus": $cpus,
  "disk": "$disk",
  "display": "$display",
  "image": "$image",
  "disk_path": "$disk_path",
  "created": "$(date -Iseconds)"
}
EOF

    log_ok "VM created: $name"
}

# ─── Start ────────────────────────────────────────────────────────────────────

# Start a VM.  Launches QEMU in the background.
# Usage: vm_start <name> [--headless] [--wait-boot] [--extra-args "..."]
vm_start() {
    local name="$1"
    shift
    local headless=true
    local wait_boot=false
    local extra_args=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --headless)    headless=true;    shift ;;
            --gui)         headless=false;   shift ;;
            --wait-boot)   wait_boot=true;   shift ;;
            --extra-args)  extra_args="$2";  shift 2 ;;
            *)             shift ;;
        esac
    done

    local dir config_file
    dir="$(vm_dir "$name")"
    config_file="$(vm_config_file "$name")"

    if [[ ! -f "$config_file" ]]; then
        log_error "VM not found: $name (no config at $config_file)"
        return 1
    fi

    # Read config.
    local ram cpus disk_path display_val
    ram=$(jq -r '.ram_mb' "$config_file")
    cpus=$(jq -r '.cpus' "$config_file")
    disk_path=$(jq -r '.disk_path' "$config_file")
    display_val=$(jq -r '.display' "$config_file")

    local qmp_sock serial_log pid_file
    qmp_sock="$(vm_qmp_socket "$name")"
    serial_log="$(vm_serial_log "$name")"
    pid_file="$(vm_pid_file "$name")"

    # Check if already running.
    if [[ -f "$pid_file" ]] && kill -0 "$(cat "$pid_file")" 2>/dev/null; then
        log_error "VM is already running: $name (PID $(cat "$pid_file"))"
        return 1
    fi

    # Remove stale socket/log.
    rm -f "$qmp_sock" "$serial_log"
    touch "$serial_log"

    # Parse display resolution.
    local width height
    width="${display_val%x*}"
    height="${display_val#*x}"

    # Build QEMU command.
    local qemu_args=(
        "$ZEN_QEMU_BIN"
        -name "$name"
        -m "${ram}"
        -smp "${cpus}"
        -drive "file=${disk_path},format=qcow2,if=virtio"
        -qmp "unix:${qmp_sock},server,nowait"
        -serial "file:${serial_log}"
        -pidfile "$pid_file"
        -daemonize
        # Enable KVM if available (WSL2 with KVM).
        -enable-kvm
        # Virtio GPU for display.
        -device virtio-vga-gl,xres="${width}",yres="${height}"
        # USB controller for hot-plug testing.
        -device qemu-xhci,id=usb-bus
        # Virtio-serial for guest agent communication.
        -device virtio-serial
        -chardev "socket,id=agent,path=${dir}/agent.sock,server=on,wait=off"
        -device "virtserialport,chardev=agent,name=org.zenos.agent"
        # Networking.
        -nic user,model=virtio-net-pci
    )

    if $headless; then
        qemu_args+=(-display none)
    else
        qemu_args+=(-display "gtk,gl=on")
    fi

    # Append any extra QEMU arguments.
    if [[ -n "$extra_args" ]]; then
        # shellcheck disable=SC2206
        qemu_args+=($extra_args)
    fi

    # Launch.
    log_info "Starting VM: $name (RAM: ${ram}MB, CPUs: ${cpus}, Display: ${display_val})"
    "${qemu_args[@]}" 2>"${dir}/qemu-stderr.log"

    # Verify launch.
    sleep 0.5
    if [[ -f "$pid_file" ]] && kill -0 "$(cat "$pid_file")" 2>/dev/null; then
        log_ok "VM started: $name (PID $(cat "$pid_file"))"
    else
        log_error "VM failed to start. Check ${dir}/qemu-stderr.log"
        cat "${dir}/qemu-stderr.log" >&2 2>/dev/null || true
        return 1
    fi

    # Optionally wait for boot.
    if $wait_boot; then
        boot_wait "$name"
        return $?
    fi

    return 0
}

# ─── Stop ─────────────────────────────────────────────────────────────────────

# Gracefully stop a VM.
# Usage: vm_stop <name> [--force] [--timeout SECONDS]
vm_stop() {
    local name="$1"
    shift
    local force=false
    local timeout=15

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --force)   force=true;    shift ;;
            --timeout) timeout="$2";  shift 2 ;;
            *)         shift ;;
        esac
    done

    local qmp_sock pid_file
    qmp_sock="$(vm_qmp_socket "$name")"
    pid_file="$(vm_pid_file "$name")"

    if [[ ! -f "$pid_file" ]]; then
        log_warn "VM not running or PID file missing: $name"
        return 0
    fi

    local pid
    pid="$(cat "$pid_file")"

    if $force; then
        kill -9 "$pid" 2>/dev/null || true
        rm -f "$pid_file"
        log_ok "VM force-killed: $name"
        return 0
    fi

    # Try graceful ACPI shutdown first.
    qmp_system_powerdown "$qmp_sock" 2>/dev/null || true

    local elapsed=0
    while (( elapsed < timeout )); do
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$pid_file"
            log_ok "VM stopped gracefully: $name (${elapsed}s)"
            return 0
        fi
        sleep 1
        elapsed=$(( elapsed + 1 ))
    done

    # Graceful shutdown failed — force quit via QMP.
    log_warn "Graceful shutdown timed out, sending QMP quit..."
    qmp_quit "$qmp_sock" 2>/dev/null || true
    sleep 1

    if kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$pid_file"
    log_ok "VM stopped (forced): $name"
}

# ─── Pause / Resume ──────────────────────────────────────────────────────────

vm_pause() {
    local name="$1"
    local qmp_sock
    qmp_sock="$(vm_qmp_socket "$name")"
    qmp_stop "$qmp_sock"
    log_ok "VM paused: $name"
}

vm_resume() {
    local name="$1"
    local qmp_sock
    qmp_sock="$(vm_qmp_socket "$name")"
    qmp_cont "$qmp_sock"
    log_ok "VM resumed: $name"
}

# ─── Destroy ──────────────────────────────────────────────────────────────────

# Destroy a VM — stop it if running, then remove all files.
# Usage: vm_destroy <name>
vm_destroy() {
    local name="$1"
    local dir
    dir="$(vm_dir "$name")"

    if [[ ! -d "$dir" ]]; then
        log_error "VM does not exist: $name"
        return 1
    fi

    # Stop if running.
    vm_stop "$name" --force 2>/dev/null || true

    rm -rf "$dir"
    log_ok "VM destroyed: $name"
}

# ─── List ─────────────────────────────────────────────────────────────────────

# List all VMs with their status.
# Output: JSON array for machine parsing.
vm_list() {
    ensure_dir "$ZEN_TEST_DIR"
    echo "["
    local first=true
    for dir in "$ZEN_TEST_DIR"/*/; do
        [[ -d "$dir" ]] || continue
        local name
        name="$(basename "$dir")"
        local config_file pid_file status
        config_file="$(vm_config_file "$name")"
        pid_file="$(vm_pid_file "$name")"

        [[ -f "$config_file" ]] || continue

        status="stopped"
        if [[ -f "$pid_file" ]] && kill -0 "$(cat "$pid_file")" 2>/dev/null; then
            local qmp_sock
            qmp_sock="$(vm_qmp_socket "$name")"
            status=$(qmp_query_status "$qmp_sock" 2>/dev/null || echo "running")
        fi

        $first || echo ","
        first=false
        echo "  {\"name\": \"$name\", \"status\": \"$status\"}"
    done
    echo "]"
}

# ─── Exec ─────────────────────────────────────────────────────────────────────

# Execute a command inside the guest via virtio-serial agent.
# Usage: vm_exec <name> <command...>
# Falls back to SSH if agent is not available.
vm_exec() {
    local name="$1"
    shift
    local agent_sock
    agent_sock="$(vm_dir "$name")/agent.sock"

    if [[ -S "$agent_sock" ]]; then
        echo "$*" | socat - "UNIX-CONNECT:${agent_sock}" 2>/dev/null
    else
        log_warn "Agent socket not available, command not sent: $*"
        log_warn "Ensure guest has zen-test-agent running on virtserialport."
        return 1
    fi
}
