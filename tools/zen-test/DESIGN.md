# zen-test — Design Document

## Overview

`zen-test` is a Rust binary that provides non-interactive, LLM-agent-friendly
QEMU VM testing infrastructure for the Zen OS project. It replaces the
bash-based `tools/zen-test-cli/` with a single compiled binary offering native
QMP, virtio-serial guest execution, serial log monitoring, screenshot analysis,
and a TOML-based quality gate framework.

**Primary users**: LLM coding agents (non-interactive, one-shot commands).
**Secondary users**: CI pipelines and human developers.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        zen-test CLI                         │
│                      (clap, main.rs)                        │
├──────┬──────┬─────────┬────────┬───────────┬───────┬────────┤
│  vm  │ gate │  image  │scenario│   ci      │output │timeout │
│      │      │         │        │           │       │        │
│create│ run  │ build   │  run   │ annotate  │ json  │watchdog│
│boot  │ list │ verify  │  list  │           │ tap   │        │
│stop  │valid.│         │        │           │ log   │        │
│destr.│      │         │        │           │       │        │
│exec  │      │         │        │           │       │        │
│screen│      │         │        │           │       │        │
│serial│      │         │        │           │       │        │
│send- │      │         │        │           │       │        │
│  key │      │         │        │           │       │        │
│list  │      │         │        │           │       │        │
├──────┴──────┴─────────┴────────┴───────────┴───────┴────────┤
│                     Core Libraries                          │
│  qmp::client   agent::exec   serial::scan   screenshot     │
│  (Unix socket)  (virtio-     (file tail +   (PPM parse +   │
│   JSON-RPC)      serial)      regex match)   blank detect)  │
└─────────────────────────────────────────────────────────────┘
         │              │              │              │
    QMP Socket     Agent Socket    Serial Log     Screenshot
    (qmp.sock)    (agent.sock)    (serial.log)     (*.ppm)
         │              │              │              │
┌─────────────────────────────────────────────────────────────┐
│                    QEMU VM Process                          │
│  ┌──────────┐  ┌──────────────┐  ┌──────┐  ┌────────────┐ │
│  │ QMP      │  │ virtio-serial│  │ttyS0 │  │ VGA/virtio │ │
│  │ Server   │  │ port         │  │      │  │ framebuffer│ │
│  └──────────┘  └──────┬───────┘  └──────┘  └────────────┘ │
│                       │                                     │
│                ┌──────┴───────┐                             │
│                │zen-test-agent│  (bash, reads commands,     │
│                │  (guest)     │   executes, returns JSON)   │
│                └──────────────┘                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Design Principles

### 1. LLM-Agent-First (Non-Interactive)

Every command is a fire-and-forget one-shot operation. No prompts, no
interactive sessions, no TUI. If a required argument is missing, exit
immediately with a JSON error.

### 2. Timeout Everything

Every blocking operation has a deadline. The global `--timeout` flag applies
to the entire command. Internal operations (QMP send, serial poll, guest exec)
each have their own sub-deadlines. If any deadline expires, the command exits
with code 2 and a `{"status":"timeout"}` JSON response.

### 3. Dual-Stream Output

| Stream | Content | Format | Use |
|--------|---------|--------|-----|
| **stdout** | Final result | Single JSON object | Agent parses this |
| **stderr** | Real-time log | JSONL (newline-delimited JSON) | Agent can ignore or stream to file |

Every stderr log line is a JSON object:
```json
{"ts":"2026-03-10T22:20:01Z","level":"info","msg":"Creating VM: test-vm"}
```

Levels: `debug`, `info`, `warn`, `error`, `ok`.

### 4. Deterministic Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Failure (assertion failed, command failed) |
| 2 | Timeout |
| 3 | Infrastructure error (QEMU missing, disk not found) |
| 4 | Invalid arguments |

### 5. Idempotent Operations

- `vm stop` on a stopped/missing VM → exit 0
- `vm destroy` on a missing VM → exit 0
- `gate run` on an already-passing gate → exit 0

### 6. Crash-Safe Cleanup

`vm destroy --force` works even when QEMU has crashed:
1. Try QMP quit
2. Try SIGTERM on PID from pidfile
3. Try SIGKILL on PID
4. Remove all socket/pid/config files
5. Always exit 0

---

## Module Specifications

### `main.rs` — CLI Entry Point

Uses `clap` derive API. Wraps every subcommand handler in:
1. Global timeout watchdog (tokio::time::timeout)
2. JSON output formatter (catches all Results, emits JSON on stdout)
3. Stderr logger setup (JSONL to stderr)

```rust
#[derive(Parser)]
#[command(name = "zen-test", about = "Zen OS VM Testing Infrastructure")]
struct Cli {
    #[command(subcommand)]
    command: Commands,

    /// Global timeout in seconds (0 = no timeout)
    #[arg(long, default_value = "120", global = true)]
    timeout: u64,

    /// Log level for stderr
    #[arg(long, default_value = "info", global = true)]
    log_level: String,
}

enum Commands {
    Vm(VmCommand),
    Gate(GateCommand),
    Image(ImageCommand),
    Scenario(ScenarioCommand),
    Version,
}
```

### `output.rs` — Structured Output

```rust
#[derive(Serialize)]
struct Output<T: Serialize> {
    status: &'static str,  // "ok", "fail", "timeout"
    #[serde(skip_serializing_if = "Option::is_none")]
    data: Option<T>,
    #[serde(skip_serializing_if = "Option::is_none")]
    error: Option<String>,
    code: i32,
    elapsed_ms: u64,
}

// Emits exactly one JSON line to stdout, then exits
fn emit_ok<T: Serialize>(data: T, elapsed: Duration) -> ! { ... }
fn emit_fail(msg: &str, elapsed: Duration) -> ! { ... }
fn emit_timeout(msg: &str, elapsed: Duration) -> ! { ... }
```

### `timeout.rs` — Global Watchdog

```rust
/// Wraps any async operation with a global deadline.
/// If the deadline expires, emits {"status":"timeout"} and exits 2.
async fn with_timeout<F, T>(seconds: u64, f: F) -> T
where F: Future<Output = T> { ... }
```

### `vm/config.rs` — VM Configuration

```rust
struct VmConfig {
    name: String,
    ram_mb: u32,       // default: 2048
    cpus: u32,         // default: 2
    disk_size: String,  // default: "32G"
    display: String,    // default: "1920x1080"
    image: PathBuf,     // base qcow2 image
    headless: bool,     // default: true
}
```

Storage layout per VM:
```
/tmp/zen-test-vms/<name>/
├── config.json     VM config snapshot
├── disk.qcow2      Overlay disk (backed by base image)
├── qmp.sock        QMP Unix socket
├── agent.sock      Virtio-serial agent socket
├── serial.log      Serial console output
├── qemu.pid        QEMU process ID
├── qemu-stderr.log QEMU stderr output
└── screenshots/    Screenshot files
```

### `vm/lifecycle.rs` — VM Lifecycle

```rust
/// Create a new VM: create overlay disk + write config.
/// Idempotent: if VM dir already exists, return error.
async fn vm_create(config: VmConfig) -> Result<VmInfo>;

/// Boot a VM: spawn QEMU, optionally wait for boot signal.
/// --wait-boot: poll serial.log for ZEN_BOOT_OK until timeout.
/// Returns immediately after boot signal or timeout.
async fn vm_boot(name: &str, wait_boot: bool, timeout: Duration) -> Result<VmBootInfo>;

/// Stop a VM gracefully (ACPI powerdown → QMP quit → SIGKILL).
async fn vm_stop(name: &str, force: bool, timeout: Duration) -> Result<()>;

/// Destroy a VM: stop + remove all files. Always succeeds.
async fn vm_destroy(name: &str) -> Result<()>;

/// List all VMs with status.
async fn vm_list() -> Result<Vec<VmStatus>>;
```

### `vm/qemu.rs` — QEMU Process Spawning

Builds the QEMU command line:
```
qemu-system-x86_64
  -name <name>
  -m <ram>
  -smp <cpus>
  -drive file=<disk>,format=qcow2,if=virtio
  -qmp unix:<qmp.sock>,server,nowait
  -serial file:<serial.log>
  -pidfile <qemu.pid>
  -daemonize
  -device qemu-xhci,id=usb-bus
  -device virtio-serial
  -chardev socket,id=agent,path=<agent.sock>,server=on,wait=off
  -device virtserialport,chardev=agent,name=org.zenos.agent
  -nic user,model=virtio-net-pci
  [-enable-kvm]                    # if /dev/kvm exists
  -device VGA,vgamem_mb=32         # headless
  -display none                    # headless
```

KVM detection: `Path::new("/dev/kvm").exists()`. If not available, use TCG
with a warning and increase boot timeout by 3x.

### `qmp/client.rs` — QMP Protocol Client

Connect-command-disconnect pattern (no persistent sessions):

```rust
struct QmpClient { socket_path: PathBuf }

impl QmpClient {
    /// Connect, negotiate capabilities, send command, read response, disconnect.
    /// Has per-command timeout (default: 5s).
    async fn execute(&self, command: &str, args: Option<Value>) -> Result<Value>;
}
```

Protocol flow per call:
1. `UnixStream::connect(socket_path)` with 2s timeout
2. Read QMP greeting JSON
3. Send `{"execute":"qmp_capabilities"}`
4. Read response
5. Send actual command
6. Read response
7. Drop connection (auto-close)

### `qmp/commands.rs` — Typed QMP Wrappers

```rust
async fn qmp_screendump(socket: &Path, output: &Path) -> Result<()>;
async fn qmp_system_powerdown(socket: &Path) -> Result<()>;
async fn qmp_quit(socket: &Path) -> Result<()>;
async fn qmp_stop(socket: &Path) -> Result<()>;
async fn qmp_cont(socket: &Path) -> Result<()>;
async fn qmp_send_key(socket: &Path, keys: &[&str]) -> Result<()>;
async fn qmp_query_status(socket: &Path) -> Result<String>;
async fn qmp_add_usb_drive(socket: &Path, image: &Path, id: &str) -> Result<()>;
async fn qmp_remove_device(socket: &Path, id: &str) -> Result<()>;
```

### `agent/exec.rs` — Guest Command Execution

```rust
/// Execute a command inside the guest via virtio-serial.
/// Returns exit_code, stdout, stderr.
/// Timeout: per-command (default: 10s).
async fn guest_exec(
    agent_socket: &Path,
    command: &str,
    timeout: Duration,
) -> Result<GuestExecResult>;

struct GuestExecResult {
    exit_code: i32,
    stdout: String,
    stderr: String,
}
```

Wire protocol (newline-delimited JSON over Unix socket → virtio-serial):
```
→ {"id":1,"cmd":"exec","args":{"command":"...","timeout":10}}
← {"id":1,"status":"ok","exit_code":0,"stdout":"...","stderr":"..."}
```

### `serial/scan.rs` — Serial Log Scanning

```rust
/// One-shot scan of serial.log for error/success patterns.
/// Non-blocking: reads the file as-is and returns immediately.
fn serial_scan(log_path: &Path) -> SerialScanResult;

struct SerialScanResult {
    boot_signal_found: bool,
    errors: Vec<PatternMatch>,   // ASan, panic, segfault, etc.
    warnings: Vec<PatternMatch>,
    line_count: usize,
}
```

### `serial/tail.rs` — Boot Wait (Poll-Based)

```rust
/// Poll serial.log for a pattern, with timeout.
/// Returns when pattern is found OR timeout expires.
/// Uses file polling (read + seek), not inotify (works on /mnt/c).
async fn serial_wait_for(
    log_path: &Path,
    pattern: &str,
    timeout: Duration,
    poll_interval: Duration,  // default: 1s
) -> Result<bool>;
```

### `serial/patterns.rs` — Error Pattern Registry

```rust
const ERROR_PATTERNS: &[(&str, &str)] = &[
    ("ERROR: AddressSanitizer",           "asan"),
    ("ERROR: LeakSanitizer",              "lsan"),
    ("ERROR: UndefinedBehaviorSanitizer",  "ubsan"),
    ("Kernel panic",                       "kernel_panic"),
    ("BUG:",                               "kernel_bug"),
    ("Oops:",                              "kernel_oops"),
    ("segfault",                           "segfault"),
    ("SIGABRT",                            "sigabrt"),
    ("assertion",                          "assertion"),
];
```

### `screenshot/capture.rs` — Screenshot Capture

```rust
/// Take a screenshot via QMP screendump. Writes to atomic temp file,
/// renames on success. Returns file metadata.
async fn screenshot_capture(
    qmp_socket: &Path,
    output: &Path,
) -> Result<ScreenshotInfo>;

struct ScreenshotInfo {
    path: PathBuf,
    size_bytes: u64,
    is_blank: bool,
    width: u32,
    height: u32,
}
```

### `screenshot/analyze.rs` — Blank Detection

```rust
/// Check if a PPM image is blank (all pixels identical).
/// Reads the raw pixel data and compares.
fn is_blank(ppm_path: &Path) -> Result<bool>;
```

### `gate/definition.rs` — TOML Gate Parser

```rust
#[derive(Deserialize)]
struct GateSpec {
    gate: GateMeta,
    vm: Option<VmSpec>,
    setup: Option<Vec<SetupStep>>,
    test: Option<Vec<TestStep>>,
    assert: Assertions,
}

#[derive(Deserialize)]
struct GateMeta {
    name: String,
    phase: String,
    sub_phase: String,
    description: Option<String>,
    tags: Option<Vec<String>>,
    timeout_seconds: Option<u64>,
}

#[derive(Deserialize)]
struct Assertions {
    serial: Option<Vec<SerialAssert>>,
    serial_absent: Option<Vec<SerialAbsentAssert>>,
    exec: Option<Vec<ExecAssert>>,
    screenshot: Option<Vec<ScreenshotAssert>>,
}
```

### `gate/runner.rs` — Gate Execution Engine

Execution flow:
```
parse TOML → create VM → boot VM → wait for boot signal
  → run setup.exec commands
  → run test.exec / test.exec_loop
  → evaluate assert.serial (pattern present in log)
  → evaluate assert.serial_absent (pattern NOT in log)
  → evaluate assert.exec (run command, check exit code)
  → evaluate assert.screenshot (capture + blank check)
  → stop VM → destroy VM → emit report
```

Every step has its own timeout. If any step times out, remaining steps
are skipped and the gate reports "timeout".

```rust
async fn run_gate(spec: &GateSpec, image: &Path) -> GateResult;

struct GateResult {
    gate: String,
    status: GateStatus,  // Passed, Failed, Timeout, Error
    assertions: Vec<AssertionResult>,
    elapsed_ms: u64,
    serial_log: Option<PathBuf>,
    screenshots: Vec<PathBuf>,
}
```

### `gate/report.rs` — Output Formatters

```rust
/// Emit JSON report to stdout.
fn emit_json(results: &[GateResult]) -> String;

/// Emit TAP v13 report.
fn emit_tap(results: &[GateResult]) -> String;
```

### `image/mod.rs` — Image Builder

```rust
/// Invoke tools/image-builder/build-test-image.sh with the compositor binary.
/// Validates that output qcow2 exists and is > 100MB.
async fn build_image(compositor: &Path, output: &Path) -> Result<ImageInfo>;

/// Quick-verify an image by booting it and checking for ZEN_BOOT_OK.
async fn verify_image(image: &Path) -> Result<bool>;
```

### `ci/mod.rs` — CI Helpers

```rust
/// Emit GitHub Actions annotation.
fn gh_error(file: &str, line: u32, msg: &str);
fn gh_warning(file: &str, line: u32, msg: &str);

/// Detect if running in CI (check GITHUB_ACTIONS env var).
fn is_ci() -> bool;
```

---

## Guest Agent

### `guest-agent/zen-test-agent` (Bash)

```bash
#!/bin/bash
set -euo pipefail
PORT="/dev/virtio-ports/org.zenos.agent"
[ -e "$PORT" ] || { echo "No virtio port" >&2; exit 1; }

while IFS= read -r line; do
    id=$(echo "$line" | jq -r '.id // 0')
    cmd=$(echo "$line" | jq -r '.args.command // ""')
    tmout=$(echo "$line" | jq -r '.args.timeout // 10')

    if [ -z "$cmd" ]; then
        printf '{"id":%d,"status":"error","message":"empty command"}\n' "$id" > "$PORT"
        continue
    fi

    out_file=$(mktemp)
    err_file=$(mktemp)
    set +e
    timeout "$tmout" bash -c "$cmd" >"$out_file" 2>"$err_file"
    ec=$?
    set -e

    stdout=$(cat "$out_file" | head -c 65536)
    stderr=$(cat "$err_file" | head -c 65536)
    rm -f "$out_file" "$err_file"

    # Escape for JSON
    stdout_json=$(printf '%s' "$stdout" | jq -Rs .)
    stderr_json=$(printf '%s' "$stderr" | jq -Rs .)

    printf '{"id":%d,"status":"ok","exit_code":%d,"stdout":%s,"stderr":%s}\n' \
        "$id" "$ec" "$stdout_json" "$stderr_json" > "$PORT"
done < "$PORT"
```

### `guest-agent/zen-test-agent.service`

```ini
[Unit]
Description=Zen OS Test Agent (virtio-serial)
After=multi-user.target
ConditionPathExists=/dev/virtio-ports/org.zenos.agent

[Service]
Type=simple
ExecStart=/usr/local/bin/zen-test-agent
Restart=always
RestartSec=2
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

---

## Quality Gate TOML Files

Located in `tools/zen-test/gates/`. One file per quality gate.

### `common/boot-baseline.toml`
Basic boot validation — used by all other gates as a prerequisite.

### `phase1/1.1-boot-signal.toml`
ZEN_BOOT_OK, clean serial log, non-blank screenshot.

### `phase1/1.2-surface-protocols.toml`
`wayland-info` lists `wl_compositor` + `wl_subcompositor`.

### `phase1/1.3-xdg-shell.toml`
Launch `weston-terminal`, resize, close. ASan clean.

### `phase1/1.4-input-routing.toml`
QMP send-key, verify characters in terminal.

### `phase1/1.5-crash-isolation.toml`
10x kill -9 cycle, compositor survives, ASan clean.

### `phase1/1.6-cairo-rendering.toml`
Screenshot analysis for "Zen OS" text overlay.

### `phase1/1.7-wallpaper.toml`
Wallpaper visible in screenshot. Fallback test.

### `phase1/1.8-keybindings.toml`
Ctrl+Alt+T launches terminal. Keys consumed.

### `phase1/1.9-layer-shell.toml`
Layer surface with exclusive zone.

### `phase1/1.10-dbus.toml`
busctl call ToggleDarkMode returns expected value.

### `phase1/1.11-session-auth.toml`
PAM auth flow, logind session visible.

### `phase1/1.12-screen-lock.toml`
Super+L → lock screen. Password entry.

### `phase1/1.13-multi-monitor.toml`
Two virtual outputs render.

### `phase1/1.14-xwayland.toml`
xeyes opens and tracks.

### `phase1/1.15-asan.toml`
LeakSanitizer reports 0 leaks.

### `scenefx/renderer-fix.toml`
WLR_RENDERER across pixman/vulkan/unset/gles2.

---

## Rust Dependencies

```toml
[package]
name = "zen-test"
version = "0.1.0"
edition = "2021"

[dependencies]
clap = { version = "4", features = ["derive"] }
serde = { version = "1", features = ["derive"] }
serde_json = "1"
toml = "0.8"
tokio = { version = "1", features = ["full"] }
nix = { version = "0.29", features = ["process", "signal"] }
regex = "1"
chrono = "0.4"
thiserror = "2"

[profile.release]
opt-level = 2
strip = true
```

Note: `image` crate removed — PPM is trivial to parse manually (header +
raw RGB bytes). Avoids a heavy dependency for one simple operation.

---

## CLI Reference

```
zen-test [--timeout <SECONDS>] [--log-level <LEVEL>] <COMMAND>

COMMANDS:
  vm create <name> [--ram 2048] [--cpus 2] [--disk 32G] [--image <path>]
  vm boot <name> [--wait-boot] [--timeout 120]
  vm stop <name> [--force] [--timeout 15]
  vm destroy <name>
  vm list
  vm exec <name> <command> [--timeout 10]
  vm screenshot <name> [--output <path>] [--timeout 10]
  vm serial-scan <name>
  vm send-key <name> <key...>

  gate run [<gate.toml>] [--phase <N>] [--tags <tag,...>]
  gate list [--phase <N>]
  gate validate <gate.toml>

  image build --compositor <path> [--output <path>] [--timeout 300]
  image verify <path> [--timeout 180]

  scenario run <name> <vm> [args...]
  scenario list

  version
```

---

## Implementation Order

1. `output.rs`, `timeout.rs` — output + timeout foundation
2. `vm/config.rs`, `vm/qemu.rs` — VM config + QEMU spawning
3. `qmp/client.rs`, `qmp/commands.rs` — QMP protocol
4. `vm/lifecycle.rs` — create/boot/stop/destroy using QMP
5. `serial/patterns.rs`, `serial/scan.rs`, `serial/tail.rs` — serial monitoring
6. `agent/exec.rs` — guest command execution
7. `screenshot/capture.rs`, `screenshot/analyze.rs` — screenshots
8. `gate/definition.rs` — TOML parsing
9. `gate/runner.rs`, `gate/report.rs` — gate execution + output
10. `main.rs` — CLI wiring
11. `image/mod.rs` — image builder
12. `ci/mod.rs` — CI annotations
13. Guest agent files
14. Quality gate TOML files
15. Integration testing
