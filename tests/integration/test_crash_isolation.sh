#!/usr/bin/env bash
# =====================================================================
#  test_crash_isolation.sh — Sub-Phase 1.5: Crash Isolation
#
#  Integration test: launch compositor + 2 clients, kill -9 one,
#  verify the other still receives input; run 10 consecutive
#  launch-and-kill cycles; grep serial log for ASan errors.
#
#  Uses zen-test (Rust binary) to orchestrate QEMU VM execution.
#  The gate TOML at tools/zen-test/gates/phase1/1.5-crash-isolation.toml
#  defines the VM-side test sequence.
#
#  Exit codes:
#    0 — all assertions passed
#    1 — one or more assertions failed
#    2 — infrastructure error (zen-test not found, image missing, etc.)
#
#  Feature: phase1-foundation
#  Property 11: crash isolation — remaining clients unaffected
#
#  Validates: Requirements 4.1–4.5
# =====================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ZEN_TEST="${REPO_ROOT}/tools/zen-test/target/release/zen-test"
GATE_TOML="${REPO_ROOT}/tools/zen-test/gates/phase1/1.5-crash-isolation.toml"
REPORT_DIR="${ZEN_REPORT_DIR:-${REPO_ROOT}/reports}"
REPORT_JSON="${REPORT_DIR}/gate-p1v05-crash-isolation.json"
TIMEOUT="${ZEN_GATE_TIMEOUT:-300}"

# ── Helpers ──────────────────────────────────────────────────────────────

log_info()  { echo "[INFO]  $*" >&2; }
log_pass()  { echo "[PASS]  $*" >&2; }
log_fail()  { echo "[FAIL]  $*" >&2; }
log_error() { echo "[ERROR] $*" >&2; }

PASS_COUNT=0
FAIL_COUNT=0

pass() { log_pass "$1"; PASS_COUNT=$((PASS_COUNT + 1)); }
fail() { log_fail "$1"; FAIL_COUNT=$((FAIL_COUNT + 1)); }

# ── Preflight ────────────────────────────────────────────────────────────

preflight() {
    if [ ! -x "$ZEN_TEST" ]; then
        log_error "zen-test binary not found or not executable: $ZEN_TEST"
        log_error "Build it with: cd tools/zen-test && cargo build --release"
        exit 2
    fi

    if [ ! -f "$GATE_TOML" ]; then
        log_error "Gate TOML not found: $GATE_TOML"
        exit 2
    fi

    mkdir -p "$REPORT_DIR"
}

# ── Run the gate via zen-test ─────────────────────────────────────────────

run_gate() {
    log_info "Running crash isolation gate..."
    log_info "  Gate: $GATE_TOML"
    log_info "  Report: $REPORT_JSON"
    log_info "  Timeout: ${TIMEOUT}s"

    local exit_code=0
    "$ZEN_TEST" gate run "$GATE_TOML" \
        --report-json "$REPORT_JSON" \
        --timeout "$TIMEOUT" \
        2>&1 || exit_code=$?

    return $exit_code
}

# ── Validate gate report ──────────────────────────────────────────────────

validate_report() {
    if [ ! -f "$REPORT_JSON" ]; then
        fail "Gate report not generated: $REPORT_JSON"
        return
    fi

    # Check overall gate status.
    local status
    status=$(python3 -c "
import json, sys
with open('$REPORT_JSON') as f:
    d = json.load(f)
print(d.get('status', 'unknown'))
" 2>/dev/null || echo "parse_error")

    if [ "$status" = "pass" ]; then
        pass "Gate status: pass"
    else
        fail "Gate status: $status (expected: pass)"
    fi

    # Check individual assertions.
    local assertions_passed assertions_failed
    assertions_passed=$(python3 -c "
import json, sys
with open('$REPORT_JSON') as f:
    d = json.load(f)
assertions = d.get('assertions', [])
print(sum(1 for a in assertions if a.get('passed', False)))
" 2>/dev/null || echo "0")

    assertions_failed=$(python3 -c "
import json, sys
with open('$REPORT_JSON') as f:
    d = json.load(f)
assertions = d.get('assertions', [])
print(sum(1 for a in assertions if not a.get('passed', True)))
" 2>/dev/null || echo "0")

    log_info "Assertions: $assertions_passed passed, $assertions_failed failed"

    if [ "$assertions_failed" -eq 0 ]; then
        pass "All gate assertions passed"
    else
        fail "$assertions_failed gate assertion(s) failed"
        # Print failing assertions for diagnosis.
        python3 -c "
import json, sys
with open('$REPORT_JSON') as f:
    d = json.load(f)
for a in d.get('assertions', []):
    if not a.get('passed', True):
        print(f'  FAIL: {a.get(\"description\", \"(no description)\")}', file=sys.stderr)
" 2>&1 || true
    fi

    # Verify compositor-still-running assertion specifically.
    local compositor_running
    compositor_running=$(python3 -c "
import json, sys
with open('$REPORT_JSON') as f:
    d = json.load(f)
for a in d.get('assertions', []):
    if 'Compositor still running' in a.get('description', ''):
        print('true' if a.get('passed', False) else 'false')
        sys.exit(0)
print('not_found')
" 2>/dev/null || echo "not_found")

    if [ "$compositor_running" = "true" ]; then
        pass "Compositor survived 10 kill cycles"
    elif [ "$compositor_running" = "false" ]; then
        fail "Compositor crashed during kill cycles"
    else
        log_info "Compositor-running assertion not found in report (may be named differently)"
    fi

    # Verify no ASan errors in serial log.
    local serial_log
    serial_log=$(python3 -c "
import json, sys
with open('$REPORT_JSON') as f:
    d = json.load(f)
print(d.get('serial_log', ''))
" 2>/dev/null || echo "")

    if [ -n "$serial_log" ] && [ -f "$serial_log" ]; then
        if grep -qE "ERROR: (AddressSanitizer|LeakSanitizer)" "$serial_log" 2>/dev/null; then
            fail "ASan/LeakSanitizer errors found in serial log"
            grep -E "ERROR: (AddressSanitizer|LeakSanitizer)" "$serial_log" | head -5 >&2 || true
        else
            pass "Serial log clean: no ASan/LeakSanitizer errors"
        fi

        if grep -q "segfault" "$serial_log" 2>/dev/null; then
            fail "Segfault detected in serial log"
        else
            pass "Serial log clean: no segfaults"
        fi

        if grep -q "SIGABRT" "$serial_log" 2>/dev/null; then
            fail "SIGABRT detected in serial log"
        else
            pass "Serial log clean: no SIGABRT"
        fi
    else
        log_info "Serial log path not in report or file missing — skipping serial checks"
    fi
}

# ── Main ─────────────────────────────────────────────────────────────────

main() {
    log_info "╔══════════════════════════════════════════════════════════╗"
    log_info "║  Sub-Phase 1.5: Crash Isolation Integration Test        ║"
    log_info "║  Feature: phase1-foundation                             ║"
    log_info "║  Property 11: crash isolation — remaining clients OK    ║"
    log_info "╚══════════════════════════════════════════════════════════╝"
    log_info ""

    preflight

    local gate_exit=0
    run_gate || gate_exit=$?

    if [ "$gate_exit" -eq 0 ]; then
        log_info "zen-test gate run exited 0"
    elif [ "$gate_exit" -eq 2 ]; then
        log_error "zen-test gate run timed out (exit 2)"
        fail "Gate timed out after ${TIMEOUT}s"
    elif [ "$gate_exit" -eq 3 ]; then
        log_error "zen-test infrastructure error (exit 3)"
        fail "Infrastructure error — check VM setup"
    else
        log_info "zen-test gate run exited $gate_exit (gate assertions may have failed)"
    fi

    validate_report

    # ── Summary ──────────────────────────────────────────────────────
    echo "" >&2
    log_info "╔══════════════════════════════════════════════════════════╗"
    log_info "║  Crash Isolation Test Results                           ║"
    log_info "╚══════════════════════════════════════════════════════════╝"
    log_info "Passed: $PASS_COUNT  Failed: $FAIL_COUNT"
    log_info "Report: $REPORT_JSON"

    if [ "$FAIL_COUNT" -eq 0 ]; then
        log_pass "OVERALL: PASS — crash isolation verified"
        exit 0
    else
        log_fail "OVERALL: FAIL — $FAIL_COUNT check(s) failed"
        exit 1
    fi
}

main "$@"
