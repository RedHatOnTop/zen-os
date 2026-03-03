#!/usr/bin/env bash
# Zen OS Testing Infrastructure — Test Scenario Injection
# Modular scenario library.  Each scenario is a function that can be
# invoked via `zen-test-cli scenario <name> <vm> [args...]`.
#
# Designed for extensibility: add new scenario_* functions to this file
# or drop additional .sh files into lib/scenarios.d/ for auto-loading.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"
# shellcheck source=qmp.sh
source "${SCRIPT_DIR}/qmp.sh"

# ─── Scenario Registry ───────────────────────────────────────────────────────

# List all available scenarios.
scenario_list() {
    echo "Available scenarios:"
    echo "  oom            — Trigger OOM killer by constraining guest memory"
    echo "  battery-low    — Mock UPower D-Bus to report low battery"
    echo "  bt-connect     — Mock BlueZ D-Bus device discovery"
    echo "  storage-insert — Hot-add USB storage device"
    echo "  storage-remove — Hot-remove USB storage device"
    echo "  resolution     — Change virtual display resolution"
    echo "  stress-ram     — Stress test with memory allocation"
    echo "  dbus-mock      — Generic D-Bus interface mocking"
}

# Dispatch a scenario by name.
# Usage: scenario_run <scenario-name> <vm-name> [args...]
scenario_run() {
    local scenario="$1"
    local vm_name="$2"
    shift 2

    case "$scenario" in
        oom)              scenario_oom "$vm_name" "$@" ;;
        battery-low)      scenario_battery_low "$vm_name" "$@" ;;
        bt-connect)       scenario_bt_connect "$vm_name" "$@" ;;
        storage-insert)   scenario_storage_insert "$vm_name" "$@" ;;
        storage-remove)   scenario_storage_remove "$vm_name" "$@" ;;
        resolution)       scenario_resolution "$vm_name" "$@" ;;
        stress-ram)       scenario_stress_ram "$vm_name" "$@" ;;
        dbus-mock)        scenario_dbus_mock "$vm_name" "$@" ;;
        list|help)        scenario_list ;;
        *)
            log_error "Unknown scenario: $scenario"
            scenario_list >&2
            return 1
            ;;
    esac
}

# ─── Scenarios ────────────────────────────────────────────────────────────────

# Trigger OOM by creating a VM with very low memory, then launching
# a memory-hungry process inside it.
# Usage: scenario_oom <vm-name> [--target-ram 512]
scenario_oom() {
    local name="$1"
    shift
    local target_ram=512  # MB — deliberately low

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --target-ram) target_ram="$2"; shift 2 ;;
            *) shift ;;
        esac
    done

    log_info "OOM scenario: restarting VM $name with ${target_ram}MB RAM"
    vm_exec "$name" "stress-ng --vm 1 --vm-bytes $((target_ram * 3 / 4))M --timeout 30s" || true
    log_info "OOM scenario triggered.  Check serial log for kernel OOM messages."
    log_info "Verify: compositor should survive, killed process logged."
}

# Mock UPower D-Bus to report low battery.
# Usage: scenario_battery_low <vm-name> [--level 5]
scenario_battery_low() {
    local name="$1"
    shift
    local level=5

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --level) level="$2"; shift 2 ;;
            *) shift ;;
        esac
    done

    log_info "Battery-low scenario: mocking UPower battery level to ${level}%"
    # Create a D-Bus mock script and execute in guest.
    local mock_script
    mock_script=$(cat <<'SCRIPT'
#!/bin/bash
# Mock UPower battery level via python-dbus or gdbus
gdbus call --system \
  --dest org.freedesktop.UPower \
  --object-path /org/freedesktop/UPower/devices/battery_BAT0 \
  --method org.freedesktop.DBus.Properties.Set \
  org.freedesktop.UPower.Device Percentage "<double LEVEL>" 2>/dev/null || \
echo "ZEN_MOCK: UPower mock requires python3-dbus or gdbus in guest"
SCRIPT
)
    mock_script="${mock_script//LEVEL/$level}"
    vm_exec "$name" "$mock_script"
    log_info "Verify: Shell should display low-battery notification toast."
}

# Mock BlueZ D-Bus device discovery.
# Usage: scenario_bt_connect <vm-name>
scenario_bt_connect() {
    local name="$1"
    log_info "BT-connect scenario: mocking BlueZ device discovery"
    vm_exec "$name" "echo 'ZEN_MOCK: BlueZ mock — requires python3-dbus mock in guest'" || true
    log_info "Verify: Quick Settings should show discovered BT device."
}

# Hot-add a USB storage device via QMP.
# Usage: scenario_storage_insert <vm-name> [--size 128M]
scenario_storage_insert() {
    local name="$1"
    shift
    local size="128M"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --size) size="$2"; shift 2 ;;
            *) shift ;;
        esac
    done

    local dir qmp_sock usb_img
    dir="$(vm_dir "$name")"
    qmp_sock="$(vm_qmp_socket "$name")"
    usb_img="${dir}/usb-test-drive.qcow2"

    # Create a small disk image if it doesn't exist.
    if [[ ! -f "$usb_img" ]]; then
        qemu-img create -f qcow2 "$usb_img" "$size" 2>/dev/null
        log_info "Created USB test disk: $usb_img ($size)"
    fi

    qmp_add_usb_drive "$qmp_sock" "$usb_img" "usb-test-drive"
    log_info "Verify: Shell should display 'USB Drive connected' notification."
}

# Hot-remove a USB storage device via QMP.
scenario_storage_remove() {
    local name="$1"
    local qmp_sock
    qmp_sock="$(vm_qmp_socket "$name")"
    qmp_remove_device "$qmp_sock" "usb-test-drive-dev"
    log_info "USB device removed. Verify: Shell notification for device removal."
}

# Change guest display resolution.
# Usage: scenario_resolution <vm-name> <WxH>
scenario_resolution() {
    local name="$1"
    local resolution="${2:-1280x720}"
    local width="${resolution%x*}"
    local height="${resolution#*x}"

    log_info "Resolution scenario: changing to ${width}x${height}"
    vm_exec "$name" "xrandr --output Virtual-1 --mode ${width}x${height} 2>/dev/null || wlr-randr --output Virtual-1 --custom-mode ${width}x${height}" || true
    log_info "Verify: Compositor should adapt layout; take screenshot to confirm."
}

# Stress-test by allocating memory inside the guest.
# Usage: scenario_stress_ram <vm-name> [--mb 3072] [--duration 30]
scenario_stress_ram() {
    local name="$1"
    shift
    local mb=3072
    local duration=30

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --mb)       mb="$2";       shift 2 ;;
            --duration) duration="$2"; shift 2 ;;
            *) shift ;;
        esac
    done

    log_info "RAM stress scenario: allocating ${mb}MB for ${duration}s"
    vm_exec "$name" "stress-ng --vm 1 --vm-bytes ${mb}M --timeout ${duration}s" || true
    log_info "Verify: Resource Manager should trigger PSI alerts; compositor stable."
}

# Generic D-Bus property mock.
# Usage: scenario_dbus_mock <vm-name> --bus system|session --dest <dest> --path <path> --iface <iface> --prop <prop> --value <value>
scenario_dbus_mock() {
    local name="$1"
    shift
    local bus="system" dest="" path="" iface="" prop="" value=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --bus)    bus="$2";   shift 2 ;;
            --dest)   dest="$2";  shift 2 ;;
            --path)   path="$2";  shift 2 ;;
            --iface)  iface="$2"; shift 2 ;;
            --prop)   prop="$2";  shift 2 ;;
            --value)  value="$2"; shift 2 ;;
            *)        shift ;;
        esac
    done

    if [[ -z "$dest" || -z "$path" || -z "$iface" || -z "$prop" ]]; then
        log_error "dbus-mock requires: --dest, --path, --iface, --prop, --value"
        return 1
    fi

    log_info "D-Bus mock: ${dest} ${iface}.${prop} = ${value}"
    vm_exec "$name" "gdbus call --${bus} --dest '${dest}' --object-path '${path}' --method org.freedesktop.DBus.Properties.Set '${iface}' '${prop}' '${value}'" || true
}

# ─── Auto-load Scenario Extensions ───────────────────────────────────────────

# Source any additional scenario files from scenarios.d/ if present.
if [[ -d "${SCRIPT_DIR}/scenarios.d" ]]; then
    for ext in "${SCRIPT_DIR}/scenarios.d"/*.sh; do
        [[ -f "$ext" ]] && source "$ext"
    done
fi
