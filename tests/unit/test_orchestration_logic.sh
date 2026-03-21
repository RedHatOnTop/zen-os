#!/usr/bin/env bash
set -euo pipefail
PASS_COUNT=0; FAIL_COUNT=0; TOTAL=0
pass() { PASS_COUNT=$((PASS_COUNT+1)); TOTAL=$((TOTAL+1)); }
fail() { echo "  FAIL: $*" >&2; FAIL_COUNT=$((FAIL_COUNT+1)); TOTAL=$((TOTAL+1)); }
VM_GATE_IDS=(p1.v01 p1.v02 p1.v03 p1.v04)
GATE_IDS=(p0.01 p0.02 p0.03 p0.04 p0.05 p1.h01 p1.v01 p1.v02)
write_gate() {
    local f=$1 g=$2 s=$3 ec=0
    [[ $s == fail ]] && ec=1; [[ $s == skip ]] && ec=null
    printf '{"gate":"%s","status":"%s","command":"cmd","exit_code":%s,"log_snippet":"log","elapsed_ms":10}\n' "$g" "$s" "$ec" >> "$f"
}
check_vm_skipped() {
    local f=$1
    for gid in "${VM_GATE_IDS[@]}"; do
        local found=false
        while IFS= read -r ln; do
            [[ -z $ln ]] && continue
            if [[ $ln == *"\"gate\":\"${gid}\""* ]]; then
                found=true
                [[ $ln != *'"status":"skip"'* ]] && { echo "wrong:${gid}"; return 1; }
                break
            fi
        done < "$f"
        $found || { echo "missing:${gid}"; return 1; }
    done
    echo ok
}
compute_exit() {
    local f=$1
    while IFS= read -r ln; do
        [[ -z $ln ]] && continue
        [[ $ln == *'"status":"fail"'* ]] && { echo 1; return; }
    done < "$f"
    echo 0
}
sim_orch() {
    local inf=$1 outf=$2 sv=${3:-false} sib=${4:-false} ztf=${5:-false}
    local cf=false
    while IFS= read -r ln; do
        [[ -z $ln ]] && continue
        [[ $ln == *'"gate":"p0.02"'* && $ln == *'"status":"fail"'* ]] && { cf=true; break; }
    done < "$inf"
    while IFS= read -r ln; do
        [[ -z $ln ]] && continue
        local iv=false
        for gid in "${VM_GATE_IDS[@]}"; do [[ $ln == *"\"gate\":\"${gid}\""* ]] && { iv=true; break; }; done
        $iv || echo "$ln" >> "$outf"
    done < "$inf"
    local vsr=
    [[ $sv == true ]] && vsr="skip-vm"
    [[ -z $vsr && $ztf == true ]] && vsr="zen-test-missing"
    [[ -z $vsr && ($cf == true || $sib == true) ]] && vsr="image-build-failed"
    for gid in "${VM_GATE_IDS[@]}"; do
        if [[ -n $vsr ]]; then
            printf '{"gate":"%s","status":"skip","command":"","exit_code":null,"log_snippet":"%s","elapsed_ms":0}\n' "$gid" "$vsr" >> "$outf"
        else
            printf '{"gate":"%s","status":"pass","command":"zen-test","exit_code":0,"log_snippet":"ok","elapsed_ms":45000}\n' "$gid" >> "$outf"
        fi
    done
}
echo '=== Property 3: Dependency-based gate skipping (100 iterations) ==='
# Feature: quality-gate-verification, Property 3: Dependency-based gate skipping
for _i in $(seq 1 100); do
    td=$(mktemp -d); inf=${td}/in.jsonl; outf=${td}/out.jsonl
    sc=$((RANDOM%4)); cs=pass; [[ $sc -eq 2 ]] && cs=fail
    write_gate "$inf" p0.01 pass; write_gate "$inf" p0.02 "$cs"; write_gate "$inf" p1.h01 pass
    sv=false; sib=false; ztf=false
    case $sc in 0) sv=true;; 1) sib=true;; 3) ztf=true;; esac
    sim_orch "$inf" "$outf" "$sv" "$sib" "$ztf"
    r=$(check_vm_skipped "$outf")
    [[ $r == ok ]] && pass || fail "i=${_i} sc=${sc}: ${r}"
    rm -rf "$td"
done
echo "  Property 3: ${PASS_COUNT} passed, ${FAIL_COUNT} failed out of ${TOTAL}"
echo
echo '=== Property 4: Exit code reflects aggregate (100 iterations) ==='
# Feature: quality-gate-verification, Property 4: Exit code reflects aggregate
P4P=0; P4F=0
for _i in $(seq 1 100); do
    td=$(mktemp -d); rf=${td}/results.jsonl
    ng=$((RANDOM%7+2)); hf=false
    for i in $(seq 1 "$ng"); do
        idx=$((RANDOM%${#GATE_IDS[@]})); g="${GATE_IDS[$idx]}-${i}"
        r=$((RANDOM%3))
        case $r in 0) s=pass;; 1) s=fail; hf=true;; 2) s=skip;; esac
        write_gate "$rf" "$g" "$s"
    done
    ee=0; $hf && ee=1
    ae=$(compute_exit "$rf")
    [[ $ae -eq $ee ]] && P4P=$((P4P+1)) || { fail "i=${_i}: ee=${ee} ae=${ae}"; P4F=$((P4F+1)); }
    rm -rf "$td"
done
PASS_COUNT=$((PASS_COUNT+P4P)); FAIL_COUNT=$((FAIL_COUNT+P4F)); TOTAL=$((TOTAL+P4P+P4F))
echo "  Property 4: ${P4P} passed, ${P4F} failed out of $((P4P+P4F))"
echo
echo '========================================'
echo ' test_orchestration_logic.sh'
echo " Total: ${TOTAL} | PASS: ${PASS_COUNT} | FAIL: ${FAIL_COUNT}"
echo '========================================'
[[ $FAIL_COUNT -gt 0 ]] && exit 1; exit 0