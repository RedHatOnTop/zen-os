#!/usr/bin/env bash
# Zen OS Testing Infrastructure — Boot Detection Library
# Robust boot detection with timeout, guest-side signal, and error scanning.
#
# Design rationale (per review feedback):
#   - Do NOT rely solely on "login:" text parsing.
#   - Guest should emit a clear ZEN_BOOT_OK signal on successful init.
#   - Strict timeout prevents infinite waits on boot failure.
#   - Error patterns (ASan, kernel panic) are checked in parallel.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

# ─── Boot Wait ────────────────────────────────────────────────────────────────

# Wait for guest to boot successfully.
# Returns 0 on success, 1 on timeout, 2 on error detected.
#
# Usage: boot_wait <vm-name> [--timeout <seconds>]
boot_wait() {
    local name="$1"
    shift
    local timeout="$ZEN_BOOT_TIMEOUT"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --timeout) timeout="$2"; shift 2 ;;
            *) shift ;;
        esac
    done

    local serial_log
    serial_log="$(vm_serial_log "$name")"
    local elapsed=0

    log_info "Waiting for boot (timeout: ${timeout}s, signal: ${ZEN_BOOT_SIGNAL})..."

    while (( elapsed < timeout )); do
        # Check for the primary boot signal first.
        if [[ -f "$serial_log" ]] && grep -q "$ZEN_BOOT_SIGNAL" "$serial_log" 2>/dev/null; then
            log_ok "Boot signal detected: $ZEN_BOOT_SIGNAL (${elapsed}s)"
            boot_scan_errors "$name"
            return 0
        fi

        # Check for the fallback signal (login prompt).
        if [[ -f "$serial_log" ]] && grep -q "$ZEN_BOOT_FALLBACK_SIGNAL" "$serial_log" 2>/dev/null; then
            log_warn "Fallback boot signal detected: $ZEN_BOOT_FALLBACK_SIGNAL (${elapsed}s)"
            log_warn "Consider adding 'echo ZEN_BOOT_OK > /dev/ttyS0' to guest init."
            boot_scan_errors "$name"
            return 0
        fi

        # Check for fatal errors while waiting.
        if boot_has_fatal_error "$name"; then
            log_error "Fatal error detected during boot at ${elapsed}s"
            boot_scan_errors "$name"
            return 2
        fi

        sleep "$ZEN_BOOT_POLL_INTERVAL"
        elapsed=$(( elapsed + ZEN_BOOT_POLL_INTERVAL ))
    done

    log_error "Boot timed out after ${timeout}s"
    log_error "Last 20 lines of serial log:"
    tail -20 "$serial_log" 2>/dev/null >&2 || log_error "(serial log not found)"
    return 1
}

# ─── Error Scanning ───────────────────────────────────────────────────────────

# Check if serial log contains any fatal error pattern.
# Returns 0 (true) if an error is found, 1 (false) if clean.
boot_has_fatal_error() {
    local name="$1"
    local serial_log
    serial_log="$(vm_serial_log "$name")"

    [[ -f "$serial_log" ]] || return 1

    for pattern in "${ZEN_ERROR_PATTERNS[@]}"; do
        if grep -q "$pattern" "$serial_log" 2>/dev/null; then
            return 0
        fi
    done
    return 1
}

# Scan serial log for all error patterns and report them.
# Non-fatal — always returns 0. Prints errors to stderr.
boot_scan_errors() {
    local name="$1"
    local serial_log
    serial_log="$(vm_serial_log "$name")"
    local found=0

    [[ -f "$serial_log" ]] || return 0

    for pattern in "${ZEN_ERROR_PATTERNS[@]}"; do
        local count
        count=$(grep -c "$pattern" "$serial_log" 2>/dev/null || echo 0)
        if (( count > 0 )); then
            log_error "Found $count occurrence(s) of: $pattern"
            found=$(( found + count ))
        fi
    done

    if (( found == 0 )); then
        log_ok "Serial log clean — no error patterns found"
    else
        log_error "Total errors found: $found"
    fi
}

# ─── Boot Report ──────────────────────────────────────────────────────────────

# Generate a JSON boot report for machine consumption.
# Usage: boot_report <vm-name> <boot-result> <elapsed-seconds>
boot_report() {
    local name="$1"
    local result="$2"  # "success", "timeout", "error"
    local elapsed="$3"
    local serial_log
    serial_log="$(vm_serial_log "$name")"
    local error_count=0

    if [[ -f "$serial_log" ]]; then
        for pattern in "${ZEN_ERROR_PATTERNS[@]}"; do
            local c
            c=$(grep -c "$pattern" "$serial_log" 2>/dev/null || echo 0)
            error_count=$(( error_count + c ))
        done
    fi

    cat <<EOF
{
  "vm": "$name",
  "result": "$result",
  "elapsed_seconds": $elapsed,
  "error_count": $error_count,
  "serial_log": "$serial_log",
  "boot_signal": "$ZEN_BOOT_SIGNAL"
}
EOF
}
