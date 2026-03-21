# Complete Rework: VM-Based Testing Infrastructure

## Problem Statement

60+ quality gates require QEMU VM testing. Current bash `zen-test-cli` is broken: no guest exec, CI dead, no assertions, fragile shell piping.

## Solution: `zen-test` — Rust Binary

Single Rust binary in `tools/zen-test/` replacing all bash scripts.

---

## Design Principle: LLM-Agent-First

> [!IMPORTANT]
> **Primary users are LLM agents**, not humans. The tool is a remote control — every command is a one-shot fire-and-forget operation. An agent must never get stuck.

### Non-Negotiable Rules

| Rule | Rationale |
|------|-----------|
| **Every command has a `--timeout` flag** (default: 120s) | VM may hang, crash, or boot loop. Agent must never wait forever. |
| **Every command exits with a deterministic code** | 0=success, 1=failure, 2=timeout, 3=VM-error. Agent can branch on exit code. |
| **All output is JSON on stdout** | No ANSI colors, no progress bars, no spinners. Machine-parseable only. |
| **Human-readable logs go to stderr only** | Agent ignores stderr; human can pipe to a file. |
| **No interactive prompts** | Never ask for input. If a required arg is missing, exit 1 with JSON error. |
| **No long-lived connections** | Every command connects, acts, disconnects. No persistent QMP sessions. |
| **Idempotent where possible** | `vm stop` on a stopped VM = success. `vm destroy` on missing VM = success. |
| **Watchdog on every blocking operation** | QMP send, serial poll, guest exec — all have per-operation deadlines. |
| **Crash-safe VM cleanup** | `vm stop --force` always works — SIGKILL by PID, delete socket files. |
| **Atomic file output** | Write to `.tmp`, rename on success. No partial screenshot files. |

### Exit Codes

```
0  — Success (gate passed, VM started, command executed)
1  — Failure (gate failed, command returned non-zero, assertion violated)
2  — Timeout (boot timeout, exec timeout, overall command timeout)
3  — Infrastructure error (QEMU binary missing, disk not found, QMP socket gone)
4  — Invalid arguments (missing required flag, bad TOML syntax)
```

### JSON Output Contract

Every command emits exactly one JSON object on stdout:

```json
// Success
{"status":"ok","data":{...},"elapsed_ms":1234}

// Failure  
{"status":"fail","error":"Gate assertion failed: no ZEN_BOOT_OK in serial","code":1,"elapsed_ms":5678}

// Timeout
{"status":"timeout","error":"Boot timeout after 120s","code":2,"elapsed_ms":120000}
```

### Agent Usage Pattern

An LLM agent interacts with `zen-test` like a series of remote-control button presses:

```bash
# 1. Build image (one-shot, exits when done)
zen-test image build --compositor ./builddir/zen-compositor --timeout 300
# → {"status":"ok","data":{"image_path":"/tmp/zen-os-test.qcow2","size_bytes":1234567}}

# 2. Boot VM (returns immediately after boot signal or timeout)
zen-test vm boot test-vm --image /tmp/zen-os-test.qcow2 --wait-boot --timeout 120
# → {"status":"ok","data":{"vm":"test-vm","pid":12345,"boot_time_ms":8500}}

# 3. Run a command inside guest (returns stdout/stderr/exit_code)
zen-test vm exec test-vm "systemctl is-active zen-compositor" --timeout 10
# → {"status":"ok","data":{"exit_code":0,"stdout":"active\n","stderr":""}}

# 4. Take screenshot (returns file path)
zen-test vm screenshot test-vm --output /tmp/boot.ppm --timeout 10
# → {"status":"ok","data":{"path":"/tmp/boot.ppm","size_bytes":6220854,"is_blank":false}}

# 5. Check serial log for errors (non-blocking scan of existing log)
zen-test vm serial-scan test-vm
# → {"status":"ok","data":{"errors":[],"warnings":[],"boot_signal_found":true}}

# 6. Run quality gate (orchestrates boot→exec→assert→screenshot→cleanup)
zen-test gate run gates/phase1/1.1-boot-signal.toml --timeout 180
# → {"status":"ok","data":{"gate":"1.1","passed":5,"failed":0,"skipped":0,"assertions":[...]}}

# 7. Cleanup (always succeeds, even if VM crashed)
zen-test vm destroy test-vm --timeout 15
# → {"status":"ok","data":{"vm":"test-vm","cleaned":true}}
```

If the VM hangs at step 2, the agent gets `{"status":"timeout",...}` after 120s and can call `vm destroy` to clean up. **The agent never gets stuck.**

---

## Project Structure

```
tools/zen-test/
├── Cargo.toml
├── src/
│   ├── main.rs              ← CLI (clap) + JSON output wrapper
│   ├── timeout.rs           ← Global timeout watchdog (tokio::time)
│   ├── output.rs            ← Structured JSON stdout + stderr logging
│   ├── vm/
│   │   ├── mod.rs
│   │   ├── config.rs        ← VM config (RAM, CPUs, disk, display)
│   │   ├── lifecycle.rs     ← create/boot/stop/destroy (all with timeout)
│   │   └── qemu.rs          ← QEMU arg builder + process spawning
│   ├── qmp/
│   │   ├── mod.rs
│   │   ├── client.rs        ← Connect → negotiate → command → disconnect
│   │   └── commands.rs      ← screendump, powerdown, quit, send-key, device_add/del
│   ├── agent/
│   │   ├── mod.rs
│   │   └── exec.rs          ← Connect → send JSON cmd → read response → disconnect
│   ├── serial/
│   │   ├── mod.rs
│   │   ├── scan.rs          ← One-shot log scan (grep patterns)
│   │   ├── tail.rs          ← Poll-based tail with deadline (for boot wait)
│   │   └── patterns.rs      ← ASan, LSan, UBSan, panic, segfault, BUG, Oops
│   ├── screenshot/
│   │   ├── mod.rs
│   │   ├── capture.rs       ← QMP screendump → atomic file write
│   │   └── analyze.rs       ← Blank detection (all-same-pixel check)
│   ├── gate/
│   │   ├── mod.rs
│   │   ├── definition.rs    ← TOML parser → GateSpec struct
│   │   ├── runner.rs        ← Execute gate: boot → setup → test → assert → cleanup
│   │   └── report.rs        ← JSON + TAP output
│   ├── image/
│   │   └── mod.rs           ← Invoke build-test-image.sh, validate output
│   └── ci/
│       └── mod.rs            ← GH Actions ::error:: annotations
├── guest-agent/
│   ├── zen-test-agent        ← Bash script for guest VM
│   └── zen-test-agent.service
└── gates/                    ← Quality gate TOML definitions
    ├── common/boot-baseline.toml
    ├── phase1/1.1-boot-signal.toml ... 1.15-asan.toml
    └── scenefx/renderer-fix.toml
```

---

## Feature Matrix

### Required (MVP)

| Feature | Description |
|---------|-------------|
| VM create/boot/stop/destroy | Full lifecycle, all with `--timeout` |
| `vm boot --wait-boot` | Poll serial for ZEN_BOOT_OK, exit on signal or timeout |
| `vm exec` | Guest command via virtio-serial, returns JSON with exit_code/stdout/stderr |
| `vm screenshot` | QMP screendump, blank detection, atomic file write |
| `vm serial-scan` | One-shot error pattern scan of serial log |
| Native QMP client | No socat — direct Unix socket JSON-RPC with per-command deadline |
| Quality gate DSL | TOML files with `[[assert.serial]]`, `[[assert.exec]]`, `[[assert.screenshot]]` |
| `gate run` | Orchestrate full boot→test→assert→cleanup cycle |
| JSON stdout | Structured output for every command |
| Deterministic exit codes | 0/1/2/3/4 — agent-friendly |
| Per-operation timeouts | Every I/O op has a deadline, never blocks forever |
| Idempotent cleanup | `vm destroy` always succeeds |
| CI annotations | GitHub Actions `::error::` / `::warning::` |
| Guest agent | Bash script in VM, systemd service, JSON protocol on virtio-serial |

### Optional (Post-MVP)

| Feature | Description |
|---------|-------------|
| `gate run --parallel` | Run independent gates concurrently |
| Screenshot pixel diff | Compare against reference screenshots |
| Flaky detection | Retry failed gates N times, report flake rate |
| JUnit XML output | For CI dashboard integration |
| D-Bus assertions | `[[assert.dbus]]` section for busctl-based checks |
| Memory budget check | `[[assert.memory]]` — check RSS ≤ threshold |
| `soak` mode | Long-running stability test |
| `vm send-key` | QMP key injection for keybinding tests |
| Boot time metrics | Report kernel → ZEN_BOOT_OK latency |

---

## Quality Gate DSL (TOML)

```toml
[gate]
name = "Sub-Phase 1.1: Boot Signal"
phase = "1"
sub_phase = "1.1"
timeout_seconds = 180  # overall gate timeout — agent never waits longer

[vm]
ram_mb = 2048
cpus = 2

[[assert.serial]]
pattern = "ZEN_BOOT_OK"

[[assert.serial_absent]]
pattern = "ERROR: AddressSanitizer"

[[assert.serial_absent]]
pattern = "Kernel panic"

[[assert.screenshot]]
type = "non_blank"
min_size_bytes = 1024
```

```toml
[gate]
name = "Sub-Phase 1.5: Crash Isolation"
phase = "1"
sub_phase = "1.5"
timeout_seconds = 300

[[setup.exec]]
command = "weston-terminal &"
timeout_seconds = 5

[[test.exec_loop]]
count = 10
command = "kill -9 $(pgrep -n weston-terminal) 2>/dev/null; sleep 1; weston-terminal &"
timeout_seconds = 10

[[assert.exec]]
command = "pgrep zen-compositor"
exit_code = 0

[[assert.serial_absent]]
pattern = "ERROR: AddressSanitizer"
```

---

## Guest Agent Protocol

Wire format (newline-delimited JSON over virtio-serial):

```
Host → Guest:  {"id":1,"cmd":"exec","args":{"command":"systemctl is-active zen-compositor","timeout":10}}
Guest → Host:  {"id":1,"status":"ok","exit_code":0,"stdout":"active\n","stderr":""}
```

Timeout handling: if no response within `timeout` seconds, the host-side `zen-test vm exec` returns `{"status":"timeout","code":2}`. The agent never hangs.

---

## Files Changed

### New
- `tools/zen-test/` — entire Rust project (~15 modules)
- `tools/zen-test/guest-agent/` — bash agent + systemd unit
- `tools/zen-test/gates/` — ~17 TOML gate definitions
- `tests/run-qemu-tests.ps1` — PowerShell → WSL wrapper

### Modified
- `tools/image-builder/build-test-image.sh` — install guest agent + jq
- `.github/workflows/qemu-test.yml` — activate with `zen-test gate run`
- `AGENTS.md` §8 — reference `zen-test`
- `docs/ALLOWED_DEPENDENCIES.md` — Rust toolchain in test-only deps

### Deprecated
- `tools/zen-test-cli/` — removed after verification

---

## Verification Plan

1. `cargo build --release && cargo test` — Rust unit tests
2. `zen-test gate run gates/common/boot-baseline.toml` — single gate integration
3. `zen-test gate run --phase 1` — all Phase 1 gates
4. Push → verify `qemu-test.yml` CI passes
