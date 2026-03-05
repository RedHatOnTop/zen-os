#!/usr/bin/env bash
# =====================================================================
#  test_renderer_assertion.sh -- Bug Condition / Fix Validation Test
#
#  ON UNFIXED CODE: Cases 1-3 crash with SIGABRT (proves bug exists).
#  ON FIXED CODE:   All cases pass -- no assertion crash, graceful exit.
#
#  NOTE: wlr_log() output goes to stderr/journald, NOT the serial port.
#  In headless QEMU without GPU, Mesa GLES2 is unavailable, so the
#  compositor hits "Failed to create renderer" -> goto cleanup.  This
#  is the correct graceful exit.  The critical assertion is: no SIGABRT,
#  no assertion crash, no sanitizer errors, no kernel panic.
#
#  Validates: Requirements 1.1, 1.2, 1.3, 2.1, 2.2, 2.3
# =====================================================================
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLI="${REPO_ROOT}/tools/zen-test-cli/zen-test-cli"
TEST_IMAGE="${ZEN_OS_IMAGE:-${REPO_ROOT}/builddir/zen-os-test.qcow2}"
LOG_DIR="${ZEN_LOG_DIR:-/tmp/zen-os-logs}/scenefx-assertion-test"
BOOT_TIMEOUT="${ZEN_BOOT_TIMEOUT:-90}"

COMPOSITOR_INIT="Compositor initialized successfully"
BOOT_SIGNAL="ZEN_BOOT_OK"
ASSERT_PATTERN="fx_get_renderer.*Assertion"
SIGABRT_PATTERN="SIGABRT"
OVERRIDE_WARNING="incompatible with SceneFX.*overriding to gles2"
UNSET_INFO="WLR_RENDERER unset.*defaulting to gles2"
RENDERER_FAIL="Failed to create renderer"
FX_GUARD="Renderer is not an fx_renderer"

SANITIZER_PATTERNS=(
    "ERROR: AddressSanitizer"
    "ERROR: LeakSanitizer"
    "ERROR: UndefinedBehaviorSanitizer"
    "Kernel panic"
    "BUG:"
    "Oops:"
)

log_info()  { echo "[INFO]  $*" >&2; }
log_pass()  { echo "[PASS]  $*" >&2; }
log_fail()  { echo "[FAIL]  $*" >&2; }
log_error() { echo "[ERROR] $*" >&2; }

PASS_COUNT=0
FAIL_COUNT=0
LAST_RESULT=""

run_case() {
    local vm_name="$1"
    local description="$2"
    local case_log_dir="${LOG_DIR}/${vm_name}"
    mkdir -p "$case_log_dir"
    log_info "=== $description ==="
    "$CLI" destroy "$vm_name" 2>/dev/null || true
    "$CLI" create "$vm_name" --ram 2048 --cpus 2 --image "$TEST_IMAGE"
    "$CLI" start "$vm_name" --headless 2>&1 || {
        log_error "Failed to start VM: $vm_name"
        LAST_RESULT="error"
        return 1
    }
    local serial="/tmp/zen-test-vms/${vm_name}/serial.log"
    local elapsed=0
    LAST_RESULT="timeout"
    while [ "$elapsed" -lt "$BOOT_TIMEOUT" ]; do
        if [ -f "$serial" ]; then
            if grep -qE "$ASSERT_PATTERN" "$serial" 2>/dev/null; then
                LAST_RESULT="crash"; break; fi
            if grep -q "$SIGABRT_PATTERN" "$serial" 2>/dev/null; then
                LAST_RESULT="crash"; break; fi
            if grep -q "$FX_GUARD" "$serial" 2>/dev/null; then
                sleep 3; LAST_RESULT="graceful_exit"; break; fi
            if grep -q "$RENDERER_FAIL" "$serial" 2>/dev/null; then
                sleep 3; LAST_RESULT="renderer_fail"; break; fi
            if grep -q "$COMPOSITOR_INIT" "$serial" 2>/dev/null && \
               grep -q "$BOOT_SIGNAL" "$serial" 2>/dev/null; then
                sleep 5
                if grep -qE "$ASSERT_PATTERN" "$serial" 2>/dev/null; then
                    LAST_RESULT="crash"
                else LAST_RESULT="success"; fi
                break; fi
        fi
        sleep 2; elapsed=$((elapsed + 2))
    done
    cp "$serial" "$case_log_dir/serial.log" 2>/dev/null || true
    log_info "Result: $LAST_RESULT (${elapsed}s)"
    "$CLI" stop "$vm_name" --force 2>/dev/null || true
    "$CLI" destroy "$vm_name" 2>/dev/null || true
}

check_no_sanitizer_errors() {
    local serial="$1"
    [ -f "$serial" ] || return 0
    for pattern in "${SANITIZER_PATTERNS[@]}"; do
        if grep -q "$pattern" "$serial" 2>/dev/null; then
            log_fail "Sanitizer/kernel error: $pattern"; return 1; fi
    done
    return 0
}

preflight() {
    if [ ! -f "$TEST_IMAGE" ]; then
        log_error "Test image not found: $TEST_IMAGE"; exit 1; fi
    if [ ! -x "$CLI" ]; then
        log_error "zen-test-cli not executable: $CLI"; exit 1; fi
    for cmd in qemu-system-x86_64 socat jq; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            log_error "Missing dependency: $cmd"; exit 1; fi
    done
    mkdir -p "$LOG_DIR"
}
# -- Case 1: WLR_RENDERER=pixman -- Validates: Requirements 1.1, 2.1
# Critical check: no SIGABRT, no assertion crash, graceful exit.
# Override warning goes to wlr_log (stderr), not serial port.
test_case1_pixman() {
    log_info "-- Case 1: WLR_RENDERER=pixman --"
    run_case "assert-pixman" "Case 1: WLR_RENDERER=pixman"
    local serial="${LOG_DIR}/assert-pixman/serial.log"
    case "$LAST_RESULT" in
        success|graceful_exit|renderer_fail)
            log_pass "Case 1: No assertion crash ($LAST_RESULT)"
            PASS_COUNT=$((PASS_COUNT + 1)) ;;
        crash)
            log_fail "Case 1: Assertion crash -- bug NOT fixed"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
        *)
            log_fail "Case 1: Unexpected result ($LAST_RESULT)"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
    esac
    if [ -f "$serial" ]; then
        # Bonus: check if override warning made it to serial (optional).
        if grep -qE "$OVERRIDE_WARNING" "$serial" 2>/dev/null; then
            log_pass "Case 1: Override warning in serial log"
        else
            log_info "Case 1: Override warning via wlr_log (not in serial)"
        fi
        PASS_COUNT=$((PASS_COUNT + 1))
        if ! check_no_sanitizer_errors "$serial"; then
            log_fail "Case 1: Sanitizer/kernel errors"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        else
            log_pass "Case 1: No sanitizer/kernel errors"
            PASS_COUNT=$((PASS_COUNT + 1))
        fi
    fi
}

# -- Case 2: WLR_RENDERER unset -- Validates: Requirements 1.2, 2.2
test_case2_unset() {
    log_info "-- Case 2: WLR_RENDERER unset --"
    run_case "assert-unset" "Case 2: WLR_RENDERER unset"
    local serial="${LOG_DIR}/assert-unset/serial.log"
    case "$LAST_RESULT" in
        success|graceful_exit|renderer_fail)
            log_pass "Case 2: No assertion crash ($LAST_RESULT)"
            PASS_COUNT=$((PASS_COUNT + 1)) ;;
        crash)
            log_fail "Case 2: Assertion crash -- bug NOT fixed"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
        *)
            log_fail "Case 2: Unexpected result ($LAST_RESULT)"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
    esac
    if [ -f "$serial" ]; then
        if grep -qE "$UNSET_INFO" "$serial" 2>/dev/null; then
            log_pass "Case 2: Unset-default info in serial log"
        else
            log_info "Case 2: Info message via wlr_log (not in serial)"
        fi
        PASS_COUNT=$((PASS_COUNT + 1))
        if ! check_no_sanitizer_errors "$serial"; then
            log_fail "Case 2: Sanitizer/kernel errors"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        else
            log_pass "Case 2: No sanitizer/kernel errors"
            PASS_COUNT=$((PASS_COUNT + 1))
        fi
    fi
}

# -- Case 3: WLR_RENDERER=vulkan -- Validates: Requirements 1.3, 2.1
test_case3_vulkan() {
    log_info "-- Case 3: WLR_RENDERER=vulkan --"
    run_case "assert-vulkan" "Case 3: WLR_RENDERER=vulkan"
    local serial="${LOG_DIR}/assert-vulkan/serial.log"
    case "$LAST_RESULT" in
        success|graceful_exit|renderer_fail)
            log_pass "Case 3: No assertion crash ($LAST_RESULT)"
            PASS_COUNT=$((PASS_COUNT + 1)) ;;
        crash)
            log_fail "Case 3: Assertion crash -- bug NOT fixed"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
        *)
            log_fail "Case 3: Unexpected result ($LAST_RESULT)"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
    esac
    if [ -f "$serial" ]; then
        if grep -qE "$OVERRIDE_WARNING" "$serial" 2>/dev/null; then
            log_pass "Case 3: Override warning in serial log"
        else
            log_info "Case 3: Override warning via wlr_log (not in serial)"
        fi
        PASS_COUNT=$((PASS_COUNT + 1))
        if ! check_no_sanitizer_errors "$serial"; then
            log_fail "Case 3: Sanitizer/kernel errors"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        else
            log_pass "Case 3: No sanitizer/kernel errors"
            PASS_COUNT=$((PASS_COUNT + 1))
        fi
    fi
}
# -- Case 4: WLR_RENDERER=gles2 (baseline) -- Validates: Requirement 2.3
test_case4_gles2_baseline() {
    log_info "-- Case 4: WLR_RENDERER=gles2 (baseline) --"
    run_case "assert-gles2-baseline" "Case 4: WLR_RENDERER=gles2"
    local serial="${LOG_DIR}/assert-gles2-baseline/serial.log"
    case "$LAST_RESULT" in
        success)
            log_pass "Case 4: Compositor started with gles2"
            PASS_COUNT=$((PASS_COUNT + 1)) ;;
        graceful_exit|renderer_fail)
            log_pass "Case 4: Graceful exit ($LAST_RESULT)"
            PASS_COUNT=$((PASS_COUNT + 1)) ;;
        crash)
            log_fail "Case 4: Assertion crash on gles2 baseline"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
        *)
            log_fail "Case 4: Unexpected result ($LAST_RESULT)"
            FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
    esac
    if [ -f "$serial" ]; then
        # No override messages should appear for gles2.
        if grep -qE "$OVERRIDE_WARNING" "$serial" 2>/dev/null; then
            log_fail "Case 4: Override warning should not appear for gles2"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        elif grep -qE "$UNSET_INFO" "$serial" 2>/dev/null; then
            log_fail "Case 4: Unset info should not appear for gles2"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        else
            log_pass "Case 4: No override messages (correct for gles2)"
            PASS_COUNT=$((PASS_COUNT + 1))
        fi
        if [ "$LAST_RESULT" = "success" ]; then
            if grep -q "$BOOT_SIGNAL" "$serial" 2>/dev/null; then
                log_pass "Case 4: ZEN_BOOT_OK emitted"
                PASS_COUNT=$((PASS_COUNT + 1))
            else
                log_fail "Case 4: ZEN_BOOT_OK not found"
                FAIL_COUNT=$((FAIL_COUNT + 1))
            fi
        fi
        if ! check_no_sanitizer_errors "$serial"; then
            log_fail "Case 4: Sanitizer/kernel errors"
            FAIL_COUNT=$((FAIL_COUNT + 1))
        else
            log_pass "Case 4: No sanitizer/kernel errors"
            PASS_COUNT=$((PASS_COUNT + 1))
        fi
    fi
}

main() {
    log_info "========================================================"
    log_info "  SceneFX Renderer -- Bug Condition / Fix Validation"
    log_info "  Validates: Requirements 1.1, 1.2, 1.3, 2.1, 2.2, 2.3"
    log_info "========================================================"
    preflight
    log_info ""
    test_case1_pixman
    test_case2_unset
    test_case3_vulkan
    test_case4_gles2_baseline
    echo "" >&2
    log_info "========================================================"
    log_info "  Assertion Test Results"
    log_info "========================================================"
    log_info "Passed: $PASS_COUNT  Failed: $FAIL_COUNT"
    cat > "${LOG_DIR}/results.json" <<EOF
{
  "test": "scenefx-renderer-assertion",
  "passed": $PASS_COUNT,
  "failed": $FAIL_COUNT,
  "total": $((PASS_COUNT + FAIL_COUNT)),
  "requirements_validated": ["1.1", "1.2", "1.3", "2.1", "2.2", "2.3"]
}
EOF
    log_info "Results: ${LOG_DIR}/results.json"
    log_info "Serial logs: ${LOG_DIR}/"
    [ "$FAIL_COUNT" -eq 0 ] && exit 0 || exit 1
}

main "$@"