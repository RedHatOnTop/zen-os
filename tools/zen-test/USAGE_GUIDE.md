# zen-test — LLM Agent Usage Guide

> This guide is for **LLM coding agents** that need to run QEMU-based tests
> on the Zen OS compositor. Every command is non-interactive and returns JSON.
> You will never get stuck — timeouts are enforced on every operation.

---

## Quick Reference

```bash
# All commands run from WSL2. The binary is at:
ZT="tools/zen-test/target/release/zen-test"

# Build (one-time)
cd tools/zen-test && cargo build --release && cd ../..

# Full workflow: build image → run gates → cleanup
$ZT image build --compositor builddir/src/compositor/zen-compositor
$ZT gate run --phase 1 --image /tmp/zen-os-test.qcow2 --gate-dir tools/zen-test/gates

# Manual workflow: create → boot → interact → destroy
$ZT vm create my-vm --image /tmp/zen-os-test.qcow2
$ZT vm boot my-vm --wait-boot --timeout 120
$ZT vm exec my-vm "systemctl is-active zen-compositor" --timeout 10
$ZT vm screenshot my-vm --output /tmp/shot.ppm
$ZT vm serial-scan my-vm
$ZT vm destroy my-vm
```

---

## Output Format

### stdout — Parse This

Every command emits exactly **one JSON object** on stdout:

```json
{"status":"ok","data":{...},"code":0,"elapsed_ms":1234}
{"status":"fail","error":"reason","code":1,"elapsed_ms":5678}
{"status":"timeout","error":"Boot timeout after 120s","code":2,"elapsed_ms":120000}
```

### stderr — Ignore or Stream to File

Real-time JSONL log lines (one per line):

```json
{"ts":"2026-03-10T22:20:01Z","level":"info","msg":"Creating VM..."}
```

### Exit Codes

| Code | Meaning | Agent Action |
|------|---------|--------------|
| 0 | Success | Continue |
| 1 | Failure (assertion failed, command error) | Investigate or retry |
| 2 | Timeout (VM hang, boot timeout, exec timeout) | Call `vm destroy`, retry |
| 3 | Infrastructure error (QEMU missing, disk not found) | Report to user |
| 4 | Invalid arguments (missing flag, bad TOML) | Fix command |

---

## Commands

### `vm create` — Create a VM

```bash
zen-test vm create <name> --image <path> [--ram 2048] [--cpus 2]
```

Creates a VM with an overlay disk backed by the given base image.
The base image is not modified.

**Output:**
```json
{"status":"ok","data":{"name":"test","ram_mb":2048,"cpus":2,"disk_path":"...","image":"..."},"code":0,"elapsed_ms":50}
```

### `vm boot` — Start a VM

```bash
zen-test vm boot <name> --wait-boot --timeout 120
```

Spawns QEMU and optionally waits for the `ZEN_BOOT_OK` signal in the serial
log. Returns immediately after the signal or when the timeout expires.

**Always use `--wait-boot`** — without it, you don't know when the VM is ready.

**Output:**
```json
{"status":"ok","data":{"name":"test","pid":12345,"kvm_enabled":true,"boot_time_ms":8500},"code":0,"elapsed_ms":9000}
```

**Timeout behavior:** If the VM hangs or boot fails, you get exit code 2:
```json
{"status":"timeout","error":"Boot timeout after 120s","code":2,"elapsed_ms":120000}
```
When this happens, call `vm destroy` to clean up, then investigate.

### `vm exec` — Run a Command in the Guest

```bash
zen-test vm exec <name> "<command>" --timeout 10
```

Executes a bash command inside the guest VM via the virtio-serial agent.
Returns the exit code, stdout, and stderr.

**Output:**
```json
{"status":"ok","data":{"exit_code":0,"stdout":"active\n","stderr":""},"code":0,"elapsed_ms":200}
```

**Common uses:**
```bash
# Check if a service is running
zen-test vm exec test "systemctl is-active zen-compositor"

# Check Wayland globals
zen-test vm exec test "wayland-info 2>/dev/null | grep wl_compositor"

# D-Bus introspection
zen-test vm exec test "busctl list | grep org.zenos"

# Check process list
zen-test vm exec test "pgrep -a zen"
```

### `vm screenshot` — Capture the Screen

```bash
zen-test vm screenshot <name> --output /tmp/shot.ppm
```

Captures the VM framebuffer via QMP screendump. Reports if the image is blank.

**Output:**
```json
{"status":"ok","data":{"path":"/tmp/shot.ppm","size_bytes":6220854,"is_blank":false},"code":0,"elapsed_ms":600}
```

### `vm serial-scan` — Scan Serial Log for Errors

```bash
zen-test vm serial-scan <name>
```

One-shot scan of the serial log for error patterns (ASan, panic, segfault)
and the boot signal. Non-blocking — reads existing log and returns immediately.

**Output:**
```json
{"status":"ok","data":{"boot_signal_found":true,"errors":[],"line_count":142},"code":0,"elapsed_ms":5}
```

### `vm send-key` — Send Key Combination

```bash
zen-test vm send-key <name> ctrl alt t
```

Sends a key combination via QMP. Useful for testing keybindings.

Key names use QEMU qcode names: `ctrl`, `alt`, `shift`, `meta_l` (Super),
`a`-`z`, `1`-`0`, `tab`, `ret`, `esc`, `f1`-`f12`, etc.

### `vm stop` — Stop a VM

```bash
zen-test vm stop <name> [--force]
```

Without `--force`: ACPI powerdown → QMP quit → SIGKILL (escalating).
With `--force`: immediate SIGKILL. Always succeeds.

### `vm destroy` — Remove a VM

```bash
zen-test vm destroy <name>
```

Stops the VM (if running) and removes all files. **Always succeeds**, even
if the VM doesn't exist or has already crashed. Use this for cleanup.

### `vm list` — List All VMs

```bash
zen-test vm list
```

**Output:**
```json
{"status":"ok","data":[{"name":"test","status":"running","pid":12345}],"code":0,"elapsed_ms":3}
```

---

## Quality Gates

### `gate run` — Run Quality Gates

```bash
# Run a single gate
zen-test gate run gates/phase1/1.1-boot-signal.toml --image /tmp/zen-os-test.qcow2

# Run all Phase 1 gates
zen-test gate run --phase 1 --image /tmp/zen-os-test.qcow2 --gate-dir tools/zen-test/gates

# Also write TAP output
zen-test gate run --phase 1 --image /tmp/zen-os.qcow2 --gate-dir tools/zen-test/gates --tap results.tap
```

Each gate orchestrates the full lifecycle:
1. Create VM (overlay disk)
2. Boot VM (wait for ZEN_BOOT_OK)
3. Run setup commands
4. Run test commands
5. Evaluate assertions (serial log, guest exec, screenshot)
6. Destroy VM (always)

**Output (single gate):**
```json
{
  "status": "ok",
  "data": {
    "gate": "Sub-Phase 1.1: Boot Signal",
    "phase": "1",
    "sub_phase": "1.1",
    "status": "passed",
    "passed": 7,
    "failed": 0,
    "assertions": [
      {"description": "Boot signal detected", "passed": true},
      {"description": "No ASan errors", "passed": true}
    ],
    "elapsed_ms": 15000
  }
}
```

**Failed gate:**
```json
{
  "status": "fail",
  "error": "1 gates: 0 passed, 1 failed",
  "data": {
    "results": [{
      "gate": "Sub-Phase 1.5: Crash Isolation",
      "status": "failed",
      "failed": 1,
      "assertions": [
        {"description": "No ASan errors", "passed": false, "detail": "Pattern found: ERROR: AddressSanitizer"}
      ]
    }]
  }
}
```

### `gate list` — List Available Gates

```bash
zen-test gate list --gate-dir tools/zen-test/gates
zen-test gate list --phase 1 --gate-dir tools/zen-test/gates
```

### `gate validate` — Check Gate TOML Syntax

```bash
zen-test gate validate gates/phase1/1.1-boot-signal.toml
```

Dry-run: parses the file and reports if it's valid, without running any VM.

---

## Image Building

### `image build` — Build Test Image

```bash
zen-test image build --compositor builddir/src/compositor/zen-compositor
```

Invokes `tools/image-builder/build-test-image.sh` to create a bootable
qcow2 with the compositor and guest agent installed.

**Requires sudo.** The image is written to `/tmp/zen-os-test.qcow2` by default.

---

## Typical Agent Workflows

### Workflow 1: Verify a Code Change

After modifying compositor source code:

```bash
# 1. Build compositor
wsl -d Ubuntu -- bash -c "cd /mnt/c/.../zen-os && meson compile -C builddir"

# 2. Build test image (requires sudo)
wsl -d Ubuntu -- bash -c "cd /mnt/c/.../zen-os && sudo tools/zen-test/target/release/zen-test image build --compositor builddir/src/compositor/zen-compositor"

# 3. Run the relevant gate
wsl -d Ubuntu -- bash -c "cd /mnt/c/.../zen-os && tools/zen-test/target/release/zen-test gate run tools/zen-test/gates/phase1/1.1-boot-signal.toml --image /tmp/zen-os-test.qcow2"

# 4. Check exit code (0 = passed, 1 = failed)
```

### Workflow 2: Debug a Boot Failure

```bash
# 1. Create and boot
$ZT vm create debug-vm --image /tmp/zen-os-test.qcow2
$ZT vm boot debug-vm --wait-boot --timeout 120

# If boot times out (exit code 2):
# 2. Scan serial log for errors
$ZT vm serial-scan debug-vm
# → Look at the "errors" array in the JSON output

# 3. Cleanup
$ZT vm destroy debug-vm
```

### Workflow 3: Test Guest Behavior

```bash
# 1. Boot a VM
$ZT vm create test --image /tmp/zen-os-test.qcow2
$ZT vm boot test --wait-boot

# 2. Check services
$ZT vm exec test "systemctl is-active zen-compositor" --timeout 10
$ZT vm exec test "systemctl status zen-compositor" --timeout 10

# 3. Check Wayland protocols
$ZT vm exec test "WAYLAND_DISPLAY=wayland-0 wayland-info" --timeout 10

# 4. Take screenshot and check for visual content
$ZT vm screenshot test --output /tmp/test-shot.ppm

# 5. Send keys and verify behavior
$ZT vm send-key test ctrl alt t
sleep 2
$ZT vm exec test "pgrep weston-terminal" --timeout 5

# 6. Cleanup
$ZT vm destroy test
```

### Workflow 4: Run All Quality Gates After Sub-Phase Completion

```bash
# Run all Phase 1 gates, write TAP report
$ZT gate run --phase 1 --image /tmp/zen-os-test.qcow2 --gate-dir tools/zen-test/gates --tap results.tap --timeout 600

# Check if all passed
echo $?  # 0 = all passed, 1 = some failed
```

---

## Error Recovery

### VM Hangs (Exit Code 2)

```bash
# Always destroy the VM after a timeout
zen-test vm destroy <name>
# Investigate serial log
cat /tmp/zen-test-vms/<name>/serial.log
# Or use: zen-test vm serial-scan <name>
```

### Agent Not Responding

If `vm exec` returns agent errors, the guest agent may not be installed:
```bash
# Check if the agent socket exists
ls -la /tmp/zen-test-vms/<name>/agent.sock
# Verify agent is installed in image (rebuild image if needed)
```

### Stale VMs

```bash
# List all VMs
zen-test vm list
# Destroy any leftover VMs
zen-test vm destroy <name>
```

---

## Writing New Quality Gates

Create a `.toml` file under `tools/zen-test/gates/`:

```toml
[gate]
name = "My New Gate"
phase = "1"
sub_phase = "1.99"
description = "What this gate validates"
tags = ["tag1", "tag2"]
timeout_seconds = 180     # Overall timeout — agent never waits longer

[vm]
ram_mb = 2048
cpus = 2

# Setup: commands to run before assertions (failures are non-fatal)
[[setup.exec]]
command = "some-setup-command"
timeout_seconds = 5

# Test: commands to verify behavior
[[test.exec]]
command = "some-test-command"
timeout_seconds = 10

# Test: repeat a command N times
[[test.exec_loop]]
count = 5
command = "repeated-command"
timeout_seconds = 10

# Assertions: ALL must pass for the gate to pass

# Pattern MUST be in serial log
[[assert.serial]]
pattern = "ZEN_BOOT_OK"
description = "Boot signal"

# Pattern must NOT be in serial log
[[assert.serial_absent]]
pattern = "ERROR: AddressSanitizer"
description = "No ASan errors"

# Guest command must exit with expected code
[[assert.exec]]
command = "pgrep zen-compositor"
exit_code = 0
description = "Compositor is running"
timeout_seconds = 5

# Screenshot must not be blank
[[assert.screenshot]]
type = "non_blank"
min_size_bytes = 1024
description = "Visual content present"
```

Validate without running: `zen-test gate validate my-gate.toml`

---

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ZEN_TEST_DIR` | `/tmp/zen-test-vms` | VM storage directory |
| `ZEN_OS_IMAGE` | (none) | Default base image path |
| `ZEN_BOOT_SIGNAL` | `ZEN_BOOT_OK` | Pattern to wait for during boot |
| `ZEN_QEMU_BIN` | `qemu-system-x86_64` | QEMU binary path |

---

## Important Notes

1. **Always destroy VMs after use.** `vm destroy` is idempotent and always succeeds.
2. **Never assume a VM is running.** Always check exit codes after `vm boot`.
3. **Use `--timeout` on long operations.** Default is 120s; increase for slow boots.
4. **Parse stdout JSON, ignore stderr.** stderr is for debugging only.
5. **The guest agent requires jq.** If `vm exec` fails, rebuild the test image.
6. **KVM improves boot time 10x.** Without KVM (no nested virt), boot takes ~60s vs ~6s.
7. **Do not use SSH.** Use `vm exec` for all guest interaction.
8. **Do not use socat for QMP.** The tool handles QMP natively.
