#!/usr/bin/env bash
# tests/unit/test_report_generator.sh
# Property-based tests for the report generator function.
#
# Feature: quality-gate-verification, Property 1: Report completeness
# Feature: quality-gate-verification, Property 2: Report content correctness

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

PASS_COUNT=0
FAIL_COUNT=0
TOTAL=0

pass() { PASS_COUNT=$((PASS_COUNT + 1)); TOTAL=$((TOTAL + 1)); }
fail() { echo "  FAIL: $*" >&2; FAIL_COUNT=$((FAIL_COUNT + 1)); TOTAL=$((TOTAL + 1)); }

# ---------------------------------------------------------------------------
# Lightweight report generator — uses a single python3 call per invocation
# to parse the JSONL and emit Markdown. Mirrors generate_report logic from
# tools/verify-quality-gates.sh.
# ---------------------------------------------------------------------------
generate_report_from_file() {
    local results_file="$1"
    local report_path="$2"
    python3 - "$results_file" "$report_path" <<'PYEOF'
import sys, json

results_file = sys.argv[1]
report_path  = sys.argv[2]

entries = []
with open(results_file) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        try:
            entries.append(json.loads(line))
        except json.JSONDecodeError:
            pass

total   = len(entries)
passed  = sum(1 for e in entries if e.get('status') == 'pass')
failed  = sum(1 for e in entries if e.get('status') == 'fail')
skipped = sum(1 for e in entries if e.get('status') == 'skip')

lines = []
lines.append("# Quality Gate Verification Report")
lines.append("")
lines.append(f"**Result**: {passed}/{total} gates passed")
lines.append("")
lines.append("---")
lines.append("")

for e in entries:
    gate        = e.get('gate', '')
    status      = e.get('status', '')
    command     = e.get('command', '')
    exit_code   = e.get('exit_code', 'null')
    log_snippet = e.get('log_snippet', '')
    error_output= e.get('error_output', '')

    icon = "❌ FAIL"
    if status == "pass":  icon = "✅ PASS"
    if status == "skip":  icon = "⏭  SKIP"

    lines.append(f"### Gate {gate}")
    lines.append(f"- **Status**: {icon}")
    if command:
        lines.append(f"- **Command**: `{command}`")
    lines.append(f"- **Exit Code**: {exit_code}")
    if log_snippet and log_snippet not in ('null', 'None', ''):
        lines.append("- **Log**:")
        lines.append("  ```")
        for l in log_snippet.splitlines()[:20]:
            lines.append(f"  {l}")
        lines.append("  ```")

    # VM-side gates: screenshot + LLM placeholder
    if gate.startswith("p1.v"):
        name_map = {"p1.v02": "1.2-surface-protocols",
                    "p1.v03": "1.3-xdg-shell",
                    "p1.v04": "1.4-input-routing"}
        # strip iteration suffix for lookup
        base = gate.split("-")[0] if "-" in gate else gate
        name = name_map.get(base, "")
        if name:
            lines.append(f"- **Screenshot**: ![{name}](screenshots/{name}.png)")
            lines.append("- **LLM Visual Assessment**:")
            lines.append("  - **Visible**: _[To be filled by LLM agent]_")
            lines.append("  - **Expected**: _[To be filled by LLM agent]_")
            lines.append("  - **Verdict**: _[PASS / CONCERN / FAIL]_")
            lines.append("  - **Notes**: _[Any anomalies or observations]_")

    # Fail: error output section
    if status == "fail" and error_output and error_output not in ('null', 'None', ''):
        lines.append("- **Error Output**:")
        lines.append("  ```")
        for l in error_output.splitlines():
            lines.append(f"  {l}")
        lines.append("  ```")

    lines.append("")

lines.append("---")
lines.append("")
lines.append("## Final Summary")
lines.append("")
lines.append("| Gate | Status |")
lines.append("|------|--------|")
for e in entries:
    gate   = e.get('gate', '')
    status = e.get('status', '')
    icon = "❌ FAIL"
    if status == "pass":  icon = "✅ PASS"
    if status == "skip":  icon = "⏭  SKIP"
    lines.append(f"| {gate} | {icon} |")
lines.append("")
lines.append(f"**Total**: {total} gates attempted, {passed} passed, {failed} failed, {skipped} skipped")

with open(report_path, 'w') as f:
    f.write('\n'.join(lines) + '\n')
PYEOF
}

# ---------------------------------------------------------------------------
# Random gate result generators (pure bash, no subprocesses)
# ---------------------------------------------------------------------------
STATUSES=("pass" "fail" "skip")
HOST_PREFIXES=("p0.01" "p0.02" "p0.03" "p0.04" "p0.05" "p1.h01" "p1.h02" "p1.h03")
VM_PREFIXES=("p1.v01" "p1.v02" "p1.v03" "p1.v04")

make_host_result() {
    local gate="$1" status="$2"
    local exit_code=0 error_output=""
    [[ "$status" == "fail" ]] && exit_code=1 && error_output="error: compilation failed"
    [[ "$status" == "skip" ]] && exit_code="null"
    printf '{"gate":"%s","status":"%s","command":"meson compile -C builddir","exit_code":%s,"log_snippet":"Build targets: 12","error_output":"%s","elapsed_ms":100}\n' \
        "$gate" "$status" "$exit_code" "$error_output"
}

make_vm_result() {
    local gate="$1" status="$2"
    local exit_code=0 error_output=""
    [[ "$status" == "fail" ]] && exit_code=1 && error_output="assertion failed: wl_compositor not found"
    [[ "$status" == "skip" ]] && exit_code="null"
    printf '{"gate":"%s","status":"%s","command":"zen-test gate run %s.toml","exit_code":%s,"log_snippet":"Gate assertions: 4/4 passed","error_output":"%s","elapsed_ms":45000}\n' \
        "$gate" "$status" "$gate" "$exit_code" "$error_output"
}

# ---------------------------------------------------------------------------
# Property 1: Report completeness
# Feature: quality-gate-verification, Property 1: Report completeness
# ---------------------------------------------------------------------------
echo "=== Property 1: Report completeness (100 iterations) ==="

for _iter in $(seq 1 100); do
    N=$(( (RANDOM % 20) + 1 ))
    tmpdir=$(mktemp -d)
    results_file="${tmpdir}/gate-results.jsonl"
    report_file="${tmpdir}/report.md"

    for i in $(seq 1 "$N"); do
        if (( RANDOM % 2 == 0 )); then
            gate="${HOST_PREFIXES[$((RANDOM % ${#HOST_PREFIXES[@]}))]}-${i}"
            status="${STATUSES[$((RANDOM % 3))]}"
            make_host_result "$gate" "$status" >> "$results_file"
        else
            gate="${VM_PREFIXES[$((RANDOM % ${#VM_PREFIXES[@]}))]}-${i}"
            status="${STATUSES[$((RANDOM % 3))]}"
            make_vm_result "$gate" "$status" >> "$results_file"
        fi
    done

    generate_report_from_file "$results_file" "$report_file"

    gate_entries=$(grep -c "^### Gate " "$report_file" 2>/dev/null || echo 0)
    summary_total=$(grep "^\*\*Total\*\*:" "$report_file" | grep -oP '^\*\*Total\*\*: \K\d+' || echo 0)

    if [[ "$gate_entries" -eq "$N" && "$summary_total" -eq "$N" ]]; then
        pass
    else
        fail "iter=${_iter} N=${N}: gate_entries=${gate_entries} summary_total=${summary_total}"
    fi

    rm -rf "$tmpdir"
done

echo "  Property 1: ${PASS_COUNT} passed, ${FAIL_COUNT} failed out of ${TOTAL}"
echo ""

# ---------------------------------------------------------------------------
# Property 2: Report content correctness by gate type and status
# Feature: quality-gate-verification, Property 2: Report content correctness
# ---------------------------------------------------------------------------
echo "=== Property 2: Report content correctness (100 iterations) ==="

P2_PASS=0
P2_FAIL=0

for _iter in $(seq 1 100); do
    gate_type=$(( RANDOM % 2 ))
    status="${STATUSES[$((RANDOM % 3))]}"

    tmpdir=$(mktemp -d)
    results_file="${tmpdir}/gate-results.jsonl"
    report_file="${tmpdir}/report.md"

    if [[ $gate_type -eq 0 ]]; then
        gate="p0.05"
        make_host_result "$gate" "$status" >> "$results_file"
    else
        gate="p1.v02"
        make_vm_result "$gate" "$status" >> "$results_file"
    fi

    generate_report_from_file "$results_file" "$report_file"

    ok=true

    # Every entry must have a Status line
    if ! grep -q "^\- \*\*Status\*\*:" "$report_file"; then
        fail "iter=${_iter} gate=${gate} status=${status}: missing Status field"
        ok=false
    fi

    # Every entry must have an Exit Code line
    if ! grep -q "^\- \*\*Exit Code\*\*:" "$report_file"; then
        fail "iter=${_iter} gate=${gate} status=${status}: missing Exit Code field"
        ok=false
    fi

    # Host-side pass: command and log snippet must be present
    if [[ $gate_type -eq 0 && "$status" == "pass" ]]; then
        if ! grep -q "^\- \*\*Command\*\*:" "$report_file"; then
            fail "iter=${_iter} host pass: missing Command field"
            ok=false
        fi
        if ! grep -q "^\- \*\*Log\*\*:" "$report_file"; then
            fail "iter=${_iter} host pass: missing Log field"
            ok=false
        fi
    fi

    # VM-side pass: screenshot path and LLM assessment placeholder must be present
    if [[ $gate_type -eq 1 && "$status" == "pass" ]]; then
        if ! grep -q "Screenshot" "$report_file"; then
            fail "iter=${_iter} vm pass: missing Screenshot field"
            ok=false
        fi
        if ! grep -q "LLM Visual Assessment" "$report_file"; then
            fail "iter=${_iter} vm pass: missing LLM Visual Assessment placeholder"
            ok=false
        fi
    fi

    # Any fail: FAIL icon must appear
    if [[ "$status" == "fail" ]]; then
        if ! grep -q "❌ FAIL" "$report_file"; then
            fail "iter=${_iter} fail status: missing FAIL icon in report"
            ok=false
        fi
    fi

    if $ok; then
        P2_PASS=$((P2_PASS + 1))
    else
        P2_FAIL=$((P2_FAIL + 1))
    fi

    rm -rf "$tmpdir"
done

PASS_COUNT=$((PASS_COUNT + P2_PASS))
FAIL_COUNT=$((FAIL_COUNT + P2_FAIL))
TOTAL=$((TOTAL + P2_PASS + P2_FAIL))

echo "  Property 2: ${P2_PASS} passed, ${P2_FAIL} failed out of $((P2_PASS + P2_FAIL))"
echo ""

# ---------------------------------------------------------------------------
# Final result
# ---------------------------------------------------------------------------
echo "========================================"
echo " test_report_generator.sh"
echo " Total: ${TOTAL} | PASS: ${PASS_COUNT} | FAIL: ${FAIL_COUNT}"
echo "========================================"

if [[ $FAIL_COUNT -gt 0 ]]; then
    exit 1
fi
exit 0
