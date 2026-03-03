# zen-test-cli

Non-interactive QEMU testing infrastructure for Zen OS, designed for agentic LLM-driven development.

## Prerequisites

- **WSL2 with KVM** (nested virtualization enabled)
  ```ini
  # ~/.wslconfig
  [wsl2]
  nestedVirtualization=true
  ```
- **Required packages**:
  ```bash
  sudo apt install qemu-system-x86 qemu-utils socat jq
  ```
- **Optional** (for stress scenarios): `sudo apt install stress-ng`

## Quick Start

```bash
# Check dependencies
./zen-test-cli check-deps

# Create a VM
./zen-test-cli create test --ram 2048 --cpus 2 --image /path/to/zen-os.qcow2

# Start headless, wait for boot
./zen-test-cli start test --headless --wait-boot

# Take a screenshot
./zen-test-cli screenshot test --output /tmp/zen-os-logs/boot.ppm

# Run a test scenario
./zen-test-cli scenario battery-low test --level 5

# Check for errors
./zen-test-cli wait-boot test  # re-scans serial log for errors

# Cleanup
./zen-test-cli stop test
./zen-test-cli destroy test
```

## Architecture

```
zen-test-cli (entry point)
├── lib/config.sh       Defaults, paths, helpers
├── lib/qmp.sh          QMP socket protocol (screendump, memdump, USB)
├── lib/boot.sh         Boot detection (timeout + guest signal + error scan)
├── lib/vm.sh           VM lifecycle (create/start/stop/pause/destroy)
├── lib/scenarios.sh    Test scenario injection (OOM, battery, USB, etc.)
└── lib/scenarios.d/    Drop-in scenario extensions (*.sh)
```

## Boot Detection

The boot detector uses a two-tier signal approach:

1. **Primary**: Guest emits `ZEN_BOOT_OK` to serial port on successful init
   ```bash
   # Add to guest init script (e.g., systemd service)
   echo "ZEN_BOOT_OK" > /dev/ttyS0
   ```
2. **Fallback**: Detects `login:` prompt in serial log
3. **Timeout**: Configurable via `ZEN_BOOT_TIMEOUT` (default: 120s)
4. **Error scan**: Checks for ASan, kernel panic, segfault patterns in parallel

## Adding Scenarios

Drop a `.sh` file into `lib/scenarios.d/`:

```bash
# lib/scenarios.d/my_scenario.sh
scenario_my_test() {
    local name="$1"
    log_info "Running my custom test on $name"
    vm_exec "$name" "my-test-command"
}
```

Then invoke: `zen-test-cli scenario my-test <vm-name>`

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `ZEN_TEST_DIR` | `/tmp/zen-test-vms` | VM storage directory |
| `ZEN_LOG_DIR` | `/tmp/zen-os-logs` | Log output directory |
| `ZEN_OS_IMAGE` | (none) | Default OS disk image |
| `ZEN_BOOT_TIMEOUT` | `120` | Boot timeout (seconds) |
| `ZEN_BOOT_SIGNAL` | `ZEN_BOOT_OK` | Guest boot signal string |
| `ZEN_QEMU_BIN` | `qemu-system-x86_64` | QEMU binary path |
