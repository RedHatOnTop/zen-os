#!/usr/bin/env bash
# tools/verify-quality-gates.sh
# Quality Gate Verification Orchestration Script
# Usage: tools/verify-quality-gates.sh [--skip-vm] [--skip-image-build] [--report-dir <path>]
# Exit codes: 0=all pass, 1=any fail, 2=infrastructure error

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# WSL_REPO is the canonical WSL path; when running inside WSL REPO_ROOT == WSL_REPO
WSL_REPO="${REPO_ROOT}"
ZEN_TEST="${REPO_ROOT}/tools/zen-test/target/release/zen-test"
IMAGE_PATH="${REPO_ROOT}/.zen-test-vms/zen-test.qcow2"
BUILDDIR="${REPO_ROOT}/builddir"

# Flags
SKIP_VM=false
SKIP_IMAGE_BUILD=false
REPORT_DIR="${REPO_ROOT}/reports"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-vm)           SKIP_VM=true; shift ;;
        --skip-image-build)  SKIP_IMAGE_BUILD=true; shift ;;
        --report-dir)        REPORT_DIR="$2"; shift 2 ;;
        *) echo "Unknown flag: $1" >&2; exit 4 ;;
    esac
done

# ---------------------------------------------------------------------------
# State tracking
# ---------------------------------------------------------------------------
RESULTS_FILE="${REPORT_DIR}/gate-results.jsonl"
SCREENSHOTS_DIR="${REPORT_DIR}/screenshots"
COMPILE_FAILED=false
ZEN_TEST_FAILED=false
IMAGE_BUILD_FAILED=false
TOTAL_GATES=0
PASSED_GATES=0
FAILED_GATES=0
SKIPPED_GATES=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
wsl_run() {
    # If already inside WSL, run directly; otherwise invoke via wsl.exe
    if grep -qi microsoft /proc/version 2>/dev/null; then
        bash -c "cd ${WSL_REPO} && $*"
    else
        wsl -d Ubuntu -- bash -c "cd ${WSL_REPO} && $*"
    fi
}

record_result() {
    local gate="$1" status="$2" command="$3" exit_code="$4" log_snippet="$5" elapsed_ms="$6"
    TOTAL_GATES=$((TOTAL_GATES + 1))
    case "$status" in
        pass)  PASSED_GATES=$((PASSED_GATES + 1)) ;;
        fail)  FAILED_GATES=$((FAILED_GATES + 1)) ;;
        skip)  SKIPPED_GATES=$((SKIPPED_GATES + 1)) ;;
    esac
    local escaped_log escaped_cmd
    escaped_log=$(printf '%s' "$log_snippet" | sed 's/\\/\\\\/g; s/"/\\"/g; s/$/\\n/' | tr -d '\n')
    escaped_cmd=$(printf '%s' "$command" | sed 's/\\/\\\\/g; s/"/\\"/g')
    printf '{"gate":"%s","status":"%s","command":"%s","exit_code":%s,"log_snippet":"%s","elapsed_ms":%s}\n' \
        "$gate" "$status" "$escaped_cmd" "$exit_code" "$escaped_log" "$elapsed_ms" \
        >> "${RESULTS_FILE}"
}

run_check() {
    local gate_id="$1" desc="$2" cmd="$3"
    local start_ms end_ms elapsed output exit_code
    start_ms=$(date +%s%3N)
    output=$(eval "$cmd" 2>&1) && exit_code=0 || exit_code=$?
    end_ms=$(date +%s%3N)
    elapsed=$((end_ms - start_ms))
    local snippet
    snippet=$(printf '%s' "$output" | tail -20)
    if [[ $exit_code -eq 0 ]]; then
        echo "  ✅ PASS  ${gate_id}: ${desc}"
        record_result "$gate_id" "pass" "$cmd" "$exit_code" "$snippet" "$elapsed"
        return 0
    else
        echo "  ❌ FAIL  ${gate_id}: ${desc}"
        record_result "$gate_id" "fail" "$cmd" "$exit_code" "$snippet" "$elapsed"
        return 1
    fi
}

skip_gate() {
    local gate_id="$1" desc="$2" reason="$3"
    echo "  ⏭  SKIP  ${gate_id}: ${desc} (${reason})"
    record_result "$gate_id" "skip" "" "null" "$reason" "0"
}

# ---------------------------------------------------------------------------
# Phase 0 Host-Side Check Functions
# ---------------------------------------------------------------------------
check_meson_setup() {
    run_check "p0.01" "Meson setup" \
        "wsl_run 'meson setup builddir --wipe 2>&1 | tail -20'"
}

check_meson_compile() {
    run_check "p0.02" "Meson compile" \
        "wsl_run 'meson compile -C builddir 2>&1 | tail -20'"
}

check_meson_test() {
    run_check "p0.03" "Meson test suite" \
        "wsl_run 'meson test -C builddir 2>&1 | tail -20'"
}

check_dbus_xml() {
    run_check "p0.04" "D-Bus XML validation" \
        "wsl_run 'xmllint --noout data/dbus/*.xml'"
}

check_systemd_units() {
    run_check "p0.05" "systemd unit files" \
        "wsl_run 'ls data/systemd/zenos-compositor.service data/systemd/zenos-resource-manager.service data/systemd/zenos-privacy-guard.service data/systemd/zenos-update-manager.service data/systemd/zenos-package-manager.service data/systemd/zenos-boot-check.service data/systemd/zenos-headless.target data/systemd/zenos-session@.service && grep -l \"\\[Unit\\]\" data/systemd/*.service | wc -l | grep -q 7 && grep -q AllowIsolate=yes data/systemd/zenos-headless.target'"
}

check_apparmor() {
    run_check "p0.06" "AppArmor profiles" \
        "wsl_run 'ls data/apparmor/ | wc -l | grep -qE \"^[6-9]|^[0-9]{2}\" && grep -l profile data/apparmor/* | wc -l | grep -q 6'"
}

check_nftables() {
    run_check "p0.07" "nftables config" \
        "wsl_run 'grep -q \"table inet filter\" data/nftables/zenos-firewall.nft && grep -q \"chain input\" data/nftables/zenos-firewall.nft && grep -q \"chain output\" data/nftables/zenos-firewall.nft'"
}

check_polkit() {
    run_check "p0.08" "polkit rules" \
        "wsl_run 'count=\$(grep -c polkit.addRule data/polkit/50-zenos.rules); [ \"\$count\" -ge 3 ]'"
}

check_zram() {
    run_check "p0.09" "zram config" \
        "wsl_run 'grep -q ALGORITHM=lz4 data/zram/zenos-zram.conf && grep -q SIZE=50 data/zram/zenos-zram.conf'"
}

check_branding() {
    run_check "p0.10" "Branding/icons" \
        "wsl_run 'grep -q \"Name=Zen OS\" data/branding/icons/index.theme && count=\$(find data/branding/icons/hicolor -type d | wc -l); [ \"\$count\" -ge 40 ]'"
}

check_browser() {
    run_check "p0.11" "Browser config" \
        "wsl_run 'python3 -m json.tool data/browser/policies.json > /dev/null && grep -q DisableTelemetry data/browser/policies.json && grep -q toolkit.telemetry data/browser/user.js && grep -qc \"{\" data/browser/userChrome.css'"
}

check_contributing() {
    run_check "p0.12" "CONTRIBUTING.md" \
        "wsl_run 'grep -qi branch docs/CONTRIBUTING.md && grep -qi commit docs/CONTRIBUTING.md && grep -qi style docs/CONTRIBUTING.md && grep -qi test docs/CONTRIBUTING.md'"
}

check_architecture() {
    run_check "p0.13" "Architecture ADRs" \
        "wsl_run 'count=\$(grep -c ADR docs/architecture/README.md); [ \"\$count\" -ge 4 ]'"
}

check_ci() {
    run_check "p0.14" "CI + clang-format" \
        "wsl_run 'grep -q \"jobs:\" .github/workflows/ci.yml && grep -q \"IndentWidth: 4\" .clang-format && grep -q \"ColumnLimit: 100\" .clang-format'"
}

check_meson_builds() {
    run_check "p0.15" "Meson build files count" \
        "wsl_run 'count=\$(find src -name meson.build | wc -l); [ \"\$count\" -ge 10 ]'"
}

# ---------------------------------------------------------------------------
# Phase 1 Host-Side Check Functions
# ---------------------------------------------------------------------------
check_test_xdg() {
    run_check "p1.h01" "test_xdg unit tests" \
        "wsl_run 'meson test -C builddir test_xdg 2>&1 | tail -20'"
}

check_test_input() {
    run_check "p1.h02" "test_input unit tests" \
        "wsl_run 'meson test -C builddir test_input 2>&1 | tail -20'"
}

check_asan_clean() {
    run_check "p1.h03" "ASan clean build" \
        "wsl_run 'ASAN_OPTIONS=detect_leaks=1 meson test -C builddir 2>&1 | grep -cE \"ERROR: (AddressSanitizer|LeakSanitizer)\" | grep -q ^0'"
}

# ---------------------------------------------------------------------------
# Gate Registries
# ---------------------------------------------------------------------------
PHASE0_GATES=(
    "p0.01|Meson setup|check_meson_setup"
    "p0.02|Meson compile|check_meson_compile"
    "p0.03|Meson test|check_meson_test"
    "p0.04|D-Bus XML validation|check_dbus_xml"
    "p0.05|systemd units|check_systemd_units"
    "p0.06|AppArmor profiles|check_apparmor"
    "p0.07|nftables config|check_nftables"
    "p0.08|polkit rules|check_polkit"
    "p0.09|zram config|check_zram"
    "p0.10|Branding/icons|check_branding"
    "p0.11|Browser config|check_browser"
    "p0.12|CONTRIBUTING.md|check_contributing"
    "p0.13|Architecture ADRs|check_architecture"
    "p0.14|CI + clang-format|check_ci"
    "p0.15|Meson build files count|check_meson_builds"
)

PHASE1_HOST_GATES=(
    "p1.h01|test_xdg unit tests|check_test_xdg"
    "p1.h02|test_input unit tests|check_test_input"
    "p1.h03|ASan clean build|check_asan_clean"
)

VM_GATES=(
    "p1.v01|Boot Baseline|tools/zen-test/gates/common/boot-baseline.toml|gate-p1v01.json"
    "p1.v02|Wayland Surface Protocols|tools/zen-test/gates/phase1/1.2-surface-protocols.toml|gate-p1v02.json"
    "p1.v03|XDG Shell Lifecycle|tools/zen-test/gates/phase1/1.3-xdg-shell.toml|gate-p1v03.json"
    "p1.v04|Input Routing|tools/zen-test/gates/phase1/1.4-input-routing.toml|gate-p1v04.json"
)

# ---------------------------------------------------------------------------
# VM_Runner
# ---------------------------------------------------------------------------
run_vm_gate() {
    local gate_id="$1" desc="$2" toml="$3" report_json="$4"
    local start_ms end_ms elapsed exit_code
    local abs_toml="${REPO_ROOT}/${toml}"
    local abs_report="${REPORT_DIR}/${report_json}"

    start_ms=$(date +%s%3N)
    # Use || true to prevent set -e from aborting the script on gate failure
    "${ZEN_TEST}" gate run "${abs_toml}" \
        --image "${IMAGE_PATH}" \
        --report-json "${abs_report}" \
        --screenshots-dir "${SCREENSHOTS_DIR}" \
        --timeout 180 \
        2>&1 | tail -5 || true
    exit_code=${PIPESTATUS[0]}
    end_ms=$(date +%s%3N)
    elapsed=$((end_ms - start_ms))

    TOTAL_GATES=$((TOTAL_GATES + 1))
    if [[ $exit_code -eq 0 ]]; then
        echo "  ✅ PASS  ${gate_id}: ${desc}"
        PASSED_GATES=$((PASSED_GATES + 1))
        record_result "$gate_id" "pass" "zen-test gate run ${toml}" "$exit_code" \
            "See ${abs_report}" "$elapsed"
        # Convert PPM screenshots to PNG
        convert_screenshots "$gate_id" "$abs_report"
    else
        echo "  ❌ FAIL  ${gate_id}: ${desc} (exit ${exit_code})"
        FAILED_GATES=$((FAILED_GATES + 1))
        record_result "$gate_id" "fail" "zen-test gate run ${toml}" "$exit_code" \
            "See ${abs_report}" "$elapsed"
    fi
}

convert_screenshots() {
    local gate_id="$1"
    # Map gate IDs to screenshot output names
    case "$gate_id" in
        p1.v01) local name="1.1-boot-baseline"      sub_phase="0.1" ;;
        p1.v02) local name="1.2-surface-protocols"  sub_phase="1.2" ;;
        p1.v03) local name="1.3-xdg-shell"          sub_phase="1.3" ;;
        p1.v04) local name="1.4-input-routing"      sub_phase="1.4" ;;
        *)      return 0 ;;
    esac
    # zen-test saves PPMs as <sub_phase>-gate-check.ppm in SCREENSHOTS_DIR
    for ppm in "${SCREENSHOTS_DIR}"/${sub_phase}-*.ppm; do
        [[ -f "$ppm" ]] || continue
        local png="${SCREENSHOTS_DIR}/${name}.png"
        if command -v convert &>/dev/null; then
            convert "$ppm" "$png" 2>/dev/null && \
                echo "    📸 Screenshot saved: ${png}"
        fi
    done
}

# ---------------------------------------------------------------------------
# Report Generator
# ---------------------------------------------------------------------------
generate_report() {
    local report_path="${REPORT_DIR}/quality-gate-report.md"
    local timestamp git_commit
    timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    git_commit=$(git -C "${REPO_ROOT}" rev-parse --short HEAD 2>/dev/null || echo "unknown")

    {
        echo "# Quality Gate Verification Report"
        echo ""
        echo "**Date**: ${timestamp}"
        echo "**Commit**: ${git_commit}"
        echo "**Result**: ${PASSED_GATES}/${TOTAL_GATES} gates passed"
        echo ""
        echo "---"
        echo ""
        echo "## Phase 0: Host-Side Verification"
        echo ""

        # Emit Phase 0 entries
        while IFS= read -r line; do
            local gate status command exit_code log_snippet
            gate=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['gate'])" 2>/dev/null) || continue
            [[ "$gate" == p0.* ]] || continue
            status=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['status'])" 2>/dev/null)
            command=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('command',''))" 2>/dev/null)
            exit_code=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('exit_code','null'))" 2>/dev/null)
            log_snippet=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('log_snippet',''))" 2>/dev/null)
            local icon="❌ FAIL"
            [[ "$status" == "pass" ]] && icon="✅ PASS"
            [[ "$status" == "skip" ]] && icon="⏭  SKIP"
            echo "### Gate ${gate}"
            echo "- **Status**: ${icon}"
            [[ -n "$command" ]] && echo "- **Command**: \`${command}\`"
            echo "- **Exit Code**: ${exit_code}"
            if [[ -n "$log_snippet" && "$log_snippet" != "null" ]]; then
                echo "- **Log**:"
                echo '  ```'
                printf '%s\n' "$log_snippet" | head -20 | sed 's/^/  /'
                echo '  ```'
            fi
            echo ""
        done < "${RESULTS_FILE}"

        echo "## Phase 1: Compositor Verification"
        echo ""
        echo "### Phase 1 Host-Side Unit Tests"
        echo ""

        while IFS= read -r line; do
            local gate status command exit_code log_snippet
            gate=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['gate'])" 2>/dev/null) || continue
            [[ "$gate" == p1.h* ]] || continue
            status=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['status'])" 2>/dev/null)
            command=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('command',''))" 2>/dev/null)
            exit_code=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('exit_code','null'))" 2>/dev/null)
            log_snippet=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('log_snippet',''))" 2>/dev/null)
            local icon="❌ FAIL"
            [[ "$status" == "pass" ]] && icon="✅ PASS"
            [[ "$status" == "skip" ]] && icon="⏭  SKIP"
            echo "### Gate ${gate}"
            echo "- **Status**: ${icon}"
            [[ -n "$command" ]] && echo "- **Command**: \`${command}\`"
            echo "- **Exit Code**: ${exit_code}"
            echo ""
        done < "${RESULTS_FILE}"

        echo "### Phase 1 VM-Side Gates"
        echo ""

        while IFS= read -r line; do
            local gate status command exit_code log_snippet
            gate=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['gate'])" 2>/dev/null) || continue
            [[ "$gate" == p1.v* ]] || continue
            status=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['status'])" 2>/dev/null)
            command=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('command',''))" 2>/dev/null)
            exit_code=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('exit_code','null'))" 2>/dev/null)
            log_snippet=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('log_snippet',''))" 2>/dev/null)
            local icon="❌ FAIL"
            [[ "$status" == "pass" ]] && icon="✅ PASS"
            [[ "$status" == "skip" ]] && icon="⏭  SKIP"
            echo "### Gate ${gate}"
            echo "- **Status**: ${icon}"
            [[ -n "$command" ]] && echo "- **Command**: \`${command}\`"
            echo "- **Exit Code**: ${exit_code}"
            # Screenshot and LLM assessment placeholder
            local name=""
            case "$gate" in
                p1.v02) name="1.2-surface-protocols" ;;
                p1.v03) name="1.3-xdg-shell" ;;
                p1.v04) name="1.4-input-routing" ;;
            esac
            if [[ -n "$name" ]]; then
                echo "- **Screenshot**: ![${name}](screenshots/${name}.png)"
                echo "- **LLM Visual Assessment**:"
                echo "  - **Visible**: _[To be filled by LLM agent]_"
                echo "  - **Expected**: _[To be filled by LLM agent]_"
                echo "  - **Verdict**: _[PASS / CONCERN / FAIL]_"
                echo "  - **Notes**: _[Any anomalies or observations]_"
            fi
            echo ""
        done < "${RESULTS_FILE}"

        # Summary table
        echo "---"
        echo ""
        echo "## Final Summary"
        echo ""
        echo "**Date**: ${timestamp}"
        echo "**Commit**: ${git_commit}"
        echo ""
        echo "| Gate | Status |"
        echo "|------|--------|"
        while IFS= read -r line; do
            local gate status icon
            gate=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['gate'])" 2>/dev/null) || continue
            status=$(printf '%s' "$line" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['status'])" 2>/dev/null)
            icon="❌ FAIL"
            [[ "$status" == "pass" ]] && icon="✅ PASS"
            [[ "$status" == "skip" ]] && icon="⏭  SKIP"
            echo "| ${gate} | ${icon} |"
        done < "${RESULTS_FILE}"
        echo ""
        echo "**Total**: ${TOTAL_GATES} gates attempted, ${PASSED_GATES} passed, ${FAILED_GATES} failed, ${SKIPPED_GATES} skipped"
    } > "${report_path}"

    echo ""
    echo "Report written to: ${report_path}"
}

# ---------------------------------------------------------------------------
# Main Execution
# ---------------------------------------------------------------------------
main() {
    mkdir -p "${REPORT_DIR}" "${SCREENSHOTS_DIR}"
    # Clear previous results
    > "${RESULTS_FILE}"

    echo "========================================"
    echo " Zen OS Quality Gate Verification"
    echo "========================================"
    echo ""

    # --- zen-test compilation check ---
    echo "[ zen-test compilation ]"
    if ! run_check "infra.zen-test" "zen-test binary" \
        "test -f '${ZEN_TEST}' && '${ZEN_TEST}' version 2>&1 | grep -q '\"name\":\"zen-test\"'"; then
        ZEN_TEST_FAILED=true
        echo "  ⚠ zen-test not available — VM gates will be skipped"
    fi
    echo ""

    # --- Phase 0: Host-Side Gates ---
    echo "[ Phase 0: Host-Side Verification ]"
    for entry in "${PHASE0_GATES[@]}"; do
        IFS='|' read -r gate_id desc fn <<< "$entry"
        if ! "$fn"; then
            # Track compile failure for dependency skipping
            [[ "$gate_id" == "p0.02" ]] && COMPILE_FAILED=true
        fi
    done
    echo ""

    # --- Phase 1 Host-Side Unit Tests ---
    echo "[ Phase 1: Host-Side Unit Tests ]"
    for entry in "${PHASE1_HOST_GATES[@]}"; do
        IFS='|' read -r gate_id desc fn <<< "$entry"
        "$fn" || true
    done
    echo ""

    # --- Image Build ---
    if [[ "$SKIP_IMAGE_BUILD" == "true" ]]; then
        echo "[ Image Build: SKIPPED (--skip-image-build) ]"
        IMAGE_BUILD_FAILED=true
    elif [[ "$COMPILE_FAILED" == "true" ]]; then
        echo "[ Image Build: SKIPPED (meson compile failed) ]"
        skip_gate "infra.image-build" "Test image build" "meson compile failed"
        IMAGE_BUILD_FAILED=true
    else
        echo "[ Image Build ]"
        if ! run_check "infra.image-build" "qcow2 image exists (>100MB)" \
            "test -f '${IMAGE_PATH}' && size=\$(du -m '${IMAGE_PATH}' | cut -f1); [ \"\$size\" -gt 100 ]"; then
            IMAGE_BUILD_FAILED=true
            echo "  ⚠ Image not ready — VM gates will be skipped"
        fi
    fi
    echo ""

    # --- VM-Side Gates ---
    if [[ "$SKIP_VM" == "true" ]]; then
        echo "[ VM-Side Gates: SKIPPED (--skip-vm) ]"
        for entry in "${VM_GATES[@]}"; do
            IFS='|' read -r gate_id desc toml report <<< "$entry"
            skip_gate "$gate_id" "$desc" "--skip-vm flag"
        done
    elif [[ "$ZEN_TEST_FAILED" == "true" ]]; then
        echo "[ VM-Side Gates: SKIPPED (zen-test unavailable) ]"
        for entry in "${VM_GATES[@]}"; do
            IFS='|' read -r gate_id desc toml report <<< "$entry"
            skip_gate "$gate_id" "$desc" "zen-test binary missing"
        done
    elif [[ "$IMAGE_BUILD_FAILED" == "true" ]]; then
        echo "[ VM-Side Gates: SKIPPED (image build failed) ]"
        for entry in "${VM_GATES[@]}"; do
            IFS='|' read -r gate_id desc toml report <<< "$entry"
            skip_gate "$gate_id" "$desc" "image build failed"
        done
    else
        echo "[ Phase 1: VM-Side Gates ]"
        for entry in "${VM_GATES[@]}"; do
            IFS='|' read -r gate_id desc toml report <<< "$entry"
            run_vm_gate "$gate_id" "$desc" "$toml" "$report"
        done
    fi
    echo ""

    # --- Report Generation ---
    echo "[ Generating Report ]"
    generate_report

    # --- Final Exit Code (Property 4) ---
    echo ""
    echo "========================================"
    echo " Total: ${TOTAL_GATES} gates | ✅ ${PASSED_GATES} passed | ❌ ${FAILED_GATES} failed | ⏭ ${SKIPPED_GATES} skipped"
    echo "========================================"

    if [[ $FAILED_GATES -gt 0 ]]; then
        exit 1
    fi
    exit 0
}

main "$@"
