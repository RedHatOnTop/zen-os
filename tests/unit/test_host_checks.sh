#!/usr/bin/env bash
# tests/unit/test_host_checks.sh
# Property-based tests for host-side config file content validation.
#
# Feature: quality-gate-verification, Property 6: Config file content validation

set -euo pipefail

PASS_COUNT=0
FAIL_COUNT=0
TOTAL=0

pass() { PASS_COUNT=$((PASS_COUNT + 1)); TOTAL=$((TOTAL + 1)); }
fail() { echo "  FAIL: $*" >&2; FAIL_COUNT=$((FAIL_COUNT + 1)); TOTAL=$((TOTAL + 1)); }

# ---------------------------------------------------------------------------
# Check functions — mirror the logic in tools/verify-quality-gates.sh
# Each returns 0 (pass) or 1 (fail).
# ---------------------------------------------------------------------------

# Systemd .service: must contain [Unit], [Service], [Install]
check_systemd_service() {
    local file="$1"
    grep -q '\[Unit\]'    "$file" && \
    grep -q '\[Service\]' "$file" && \
    grep -q '\[Install\]' "$file"
}

# AppArmor profile: must contain a "profile" block with at least one file rule
check_apparmor_profile() {
    local file="$1"
    grep -q 'profile ' "$file" && \
    grep -qE '^\s+/[^ ]+ [rwmkl]+,' "$file"
}

# nftables: must contain "table inet filter", "chain input", "chain output"
check_nftables_config() {
    local file="$1"
    grep -q 'table inet filter' "$file" && \
    grep -q 'chain input'       "$file" && \
    grep -q 'chain output'      "$file"
}

# polkit: must contain at least 3 polkit.addRule calls
check_polkit_rules() {
    local file="$1"
    local count
    count=$(grep -c 'polkit\.addRule' "$file" 2>/dev/null || echo 0)
    [[ "$count" -ge 3 ]]
}

# zram: must contain ALGORITHM=lz4 and SIZE=50
check_zram_config() {
    local file="$1"
    grep -q 'ALGORITHM=lz4' "$file" && \
    grep -q 'SIZE=50'       "$file"
}

# ---------------------------------------------------------------------------
# Helper: write a temp file with given content, run check, expect result
# ---------------------------------------------------------------------------
assert_check() {
    local check_fn="$1"
    local expected="$2"   # "pass" or "fail"
    local content="$3"
    local label="$4"

    local tmpfile
    tmpfile=$(mktemp)
    printf '%s' "$content" > "$tmpfile"

    local actual_exit=0
    "$check_fn" "$tmpfile" > /dev/null 2>&1 || actual_exit=$?

    local actual="fail"
    [[ $actual_exit -eq 0 ]] && actual="pass"

    rm -f "$tmpfile"

    if [[ "$actual" == "$expected" ]]; then
        pass
    else
        fail "${label}: expected=${expected} actual=${actual}"
    fi
}

# ---------------------------------------------------------------------------
# Property 6: Config file content validation
# For each config type, verify check returns pass iff all required patterns
# are present, and fail if any required pattern is missing.
#
# Feature: quality-gate-verification, Property 6: Config file content validation
# ---------------------------------------------------------------------------

# --- Systemd .service (100 iterations) ---
echo "=== Property 6a: systemd .service validation (100 iterations) ==="

SYSTEMD_REQUIRED=("[Unit]" "[Service]" "[Install]")

for _iter in $(seq 1 100); do
    # Randomly decide whether to include all required sections or drop one
    r=$(( RANDOM % 4 ))
    case $r in
        0)
            # All present → pass
            content="[Unit]
Description=Test
[Service]
ExecStart=/bin/true
[Install]
WantedBy=multi-user.target"
            assert_check check_systemd_service "pass" "$content" "systemd iter=${_iter} all-present"
            ;;
        1)
            # Missing [Unit] → fail
            content="[Service]
ExecStart=/bin/true
[Install]
WantedBy=multi-user.target"
            assert_check check_systemd_service "fail" "$content" "systemd iter=${_iter} missing-Unit"
            ;;
        2)
            # Missing [Service] → fail
            content="[Unit]
Description=Test
[Install]
WantedBy=multi-user.target"
            assert_check check_systemd_service "fail" "$content" "systemd iter=${_iter} missing-Service"
            ;;
        3)
            # Missing [Install] → fail
            content="[Unit]
Description=Test
[Service]
ExecStart=/bin/true"
            assert_check check_systemd_service "fail" "$content" "systemd iter=${_iter} missing-Install"
            ;;
    esac
done

echo "  Property 6a: ${PASS_COUNT} passed, ${FAIL_COUNT} failed out of ${TOTAL}"
echo ""

# --- AppArmor profile (100 iterations) ---
echo "=== Property 6b: AppArmor profile validation (100 iterations) ==="

P6B_START=$TOTAL

for _iter in $(seq 1 100); do
    r=$(( RANDOM % 3 ))
    case $r in
        0)
            # Valid profile with file rule → pass
            content="profile zenos-test /usr/bin/test {
  /etc/passwd r,
  /usr/lib/** mr,
}"
            assert_check check_apparmor_profile "pass" "$content" "apparmor iter=${_iter} valid"
            ;;
        1)
            # Missing "profile" keyword → fail
            content="{
  /etc/passwd r,
  /usr/lib/** mr,
}"
            assert_check check_apparmor_profile "fail" "$content" "apparmor iter=${_iter} no-profile-keyword"
            ;;
        2)
            # Has profile keyword but no file rule → fail
            content="profile zenos-test /usr/bin/test {
  capability sys_nice,
}"
            assert_check check_apparmor_profile "fail" "$content" "apparmor iter=${_iter} no-file-rule"
            ;;
    esac
done

P6B_COUNT=$((TOTAL - P6B_START))
P6B_PASS=$((PASS_COUNT - (P6B_START > 0 ? P6B_START : 0)))
echo "  Property 6b: completed ${P6B_COUNT} iterations"
echo ""

# --- nftables config (100 iterations) ---
echo "=== Property 6c: nftables config validation (100 iterations) ==="

P6C_START=$TOTAL

for _iter in $(seq 1 100); do
    r=$(( RANDOM % 4 ))
    case $r in
        0)
            # All required → pass
            content="table inet filter {
    chain input { type filter hook input priority 0; policy drop; }
    chain output { type filter hook output priority 0; policy accept; }
}"
            assert_check check_nftables_config "pass" "$content" "nftables iter=${_iter} valid"
            ;;
        1)
            # Missing table inet filter → fail
            content="chain input { type filter hook input priority 0; }
chain output { type filter hook output priority 0; }"
            assert_check check_nftables_config "fail" "$content" "nftables iter=${_iter} no-table"
            ;;
        2)
            # Missing chain input → fail
            content="table inet filter {
    chain output { type filter hook output priority 0; policy accept; }
}"
            assert_check check_nftables_config "fail" "$content" "nftables iter=${_iter} no-chain-input"
            ;;
        3)
            # Missing chain output → fail
            content="table inet filter {
    chain input { type filter hook input priority 0; policy drop; }
}"
            assert_check check_nftables_config "fail" "$content" "nftables iter=${_iter} no-chain-output"
            ;;
    esac
done

P6C_COUNT=$((TOTAL - P6C_START))
echo "  Property 6c: completed ${P6C_COUNT} iterations"
echo ""

# --- polkit rules (100 iterations) ---
echo "=== Property 6d: polkit rules validation (100 iterations) ==="

P6D_START=$TOTAL

for _iter in $(seq 1 100); do
    r=$(( RANDOM % 3 ))
    case $r in
        0)
            # 3 or more rules → pass
            n=$(( (RANDOM % 5) + 3 ))
            content=""
            for i in $(seq 1 "$n"); do
                content="${content}polkit.addRule(function(action, subject) { return polkit.Result.AUTH_ADMIN; });
"
            done
            assert_check check_polkit_rules "pass" "$content" "polkit iter=${_iter} n=${n}"
            ;;
        1)
            # 0 rules → fail
            content="// no rules here"
            assert_check check_polkit_rules "fail" "$content" "polkit iter=${_iter} zero-rules"
            ;;
        2)
            # 1 or 2 rules → fail
            n=$(( (RANDOM % 2) + 1 ))
            content=""
            for i in $(seq 1 "$n"); do
                content="${content}polkit.addRule(function(action, subject) { return polkit.Result.AUTH_ADMIN; });
"
            done
            assert_check check_polkit_rules "fail" "$content" "polkit iter=${_iter} n=${n}"
            ;;
    esac
done

P6D_COUNT=$((TOTAL - P6D_START))
echo "  Property 6d: completed ${P6D_COUNT} iterations"
echo ""

# --- zram config (100 iterations) ---
echo "=== Property 6e: zram config validation (100 iterations) ==="

P6E_START=$TOTAL

for _iter in $(seq 1 100); do
    r=$(( RANDOM % 4 ))
    case $r in
        0)
            # Both present → pass
            content="ALGORITHM=lz4
SIZE=50
STREAMS=auto"
            assert_check check_zram_config "pass" "$content" "zram iter=${_iter} valid"
            ;;
        1)
            # Missing ALGORITHM → fail
            content="SIZE=50
STREAMS=auto"
            assert_check check_zram_config "fail" "$content" "zram iter=${_iter} no-algorithm"
            ;;
        2)
            # Missing SIZE → fail
            content="ALGORITHM=lz4
STREAMS=auto"
            assert_check check_zram_config "fail" "$content" "zram iter=${_iter} no-size"
            ;;
        3)
            # Wrong algorithm → fail
            content="ALGORITHM=zstd
SIZE=50"
            assert_check check_zram_config "fail" "$content" "zram iter=${_iter} wrong-algorithm"
            ;;
    esac
done

P6E_COUNT=$((TOTAL - P6E_START))
echo "  Property 6e: completed ${P6E_COUNT} iterations"
echo ""

# ---------------------------------------------------------------------------
# Final result
# ---------------------------------------------------------------------------
echo "========================================"
echo " test_host_checks.sh"
echo " Total: ${TOTAL} | PASS: ${PASS_COUNT} | FAIL: ${FAIL_COUNT}"
echo "========================================"

if [[ $FAIL_COUNT -gt 0 ]]; then
    exit 1
fi
exit 0
