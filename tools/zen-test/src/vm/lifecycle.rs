// zen-test — VM lifecycle: create, boot, stop, destroy, list, exec, screenshot, serial-scan.

use crate::errors::{Result, ZenTestError};
use crate::output;
use crate::serial;
use crate::vm::config::*;
use crate::vm::qemu;
use chrono::Utc;
use std::path::Path;
use std::time::{Duration, Instant};

/// Create a new VM.
pub fn vm_create(
    name: &str,
    ram_mb: u32,
    cpus: u32,
    disk_size: &str,
    display: &str,
    image: Option<&Path>,
) -> Result<VmInfo> {
    let paths = VmPaths::new(name);

    if paths.exists() {
        return Err(ZenTestError::VmAlreadyExists(name.to_string()));
    }

    std::fs::create_dir_all(&paths.dir)?;
    std::fs::create_dir_all(&paths.screenshots_dir)?;

    // Create disk.
    let image_path = image
        .map(|p| p.to_path_buf())
        .or_else(|| std::env::var("ZEN_OS_IMAGE").ok().map(|s| s.into()));

    if let Some(ref base) = image_path {
        if !base.exists() {
            return Err(ZenTestError::InfraError(format!(
                "Base image not found: {}",
                base.display()
            )));
        }
        qemu::create_overlay_disk(&paths.disk, base, disk_size)?;
    } else {
        qemu::create_blank_disk(&paths.disk, disk_size)?;
    }

    // Write config.
    let config = VmConfig {
        name: name.to_string(),
        ram_mb,
        cpus,
        disk_size: disk_size.to_string(),
        display: display.to_string(),
        image: image_path.clone().unwrap_or_default(),
        disk_path: paths.disk.clone(),
        created: Utc::now().to_rfc3339(),
    };

    let config_json = serde_json::to_string_pretty(&config)?;
    std::fs::write(&paths.config_file, config_json)?;

    output::log_ok(&format!("VM created: {name}"));

    Ok(VmInfo {
        name: name.to_string(),
        ram_mb,
        cpus,
        disk_path: paths.disk.display().to_string(),
        image: image_path.map(|p| p.display().to_string()).unwrap_or_default(),
    })
}

/// Boot a VM: spawn QEMU, optionally wait for boot signal.
pub async fn vm_boot(
    name: &str,
    wait_boot: bool,
    timeout_secs: u64,
) -> Result<VmBootInfo> {
    let paths = VmPaths::new(name);

    if !paths.exists() {
        return Err(ZenTestError::VmNotFound(name.to_string()));
    }

    if paths.is_running() {
        return Err(ZenTestError::VmAlreadyRunning(name.to_string()));
    }

    // Read config.
    let config_str = std::fs::read_to_string(&paths.config_file)?;
    let config: VmConfig = serde_json::from_str(&config_str)?;

    // Clean stale files.
    let _ = std::fs::remove_file(&paths.qmp_socket);
    let _ = std::fs::remove_file(&paths.serial_log);
    std::fs::write(&paths.serial_log, "")?;

    // Build and spawn QEMU.
    let qemu_bin = qemu::check_qemu_binary()?;
    let kvm = qemu::kvm_available();
    let args = qemu::build_qemu_args(
        &qemu_bin,
        name,
        config.ram_mb,
        config.cpus,
        &config.disk_path,
        &config.display,
        &paths,
        true, // always headless in agent mode
    );

    qemu::spawn_qemu(&args, &paths.qemu_stderr)?;

    // Wait briefly for QEMU to write pidfile.
    tokio::time::sleep(Duration::from_millis(500)).await;

    let pid = paths.read_pid().ok_or_else(|| {
        let stderr = std::fs::read_to_string(&paths.qemu_stderr).unwrap_or_default();
        ZenTestError::QemuStartFailed(format!("No PID file. QEMU stderr: {stderr}"))
    })?;

    if !Path::new(&format!("/proc/{pid}")).exists() {
        let stderr = std::fs::read_to_string(&paths.qemu_stderr).unwrap_or_default();
        return Err(ZenTestError::QemuStartFailed(format!(
            "QEMU exited immediately (PID {pid}). stderr: {stderr}"
        )));
    }

    output::log_ok(&format!("VM started: {name} (PID {pid}, KVM: {kvm})"));

    let mut boot_time_ms = None;

    if wait_boot {
        let boot_start = Instant::now();
        let boot_timeout = if kvm { timeout_secs } else { timeout_secs * 3 };
        output::log_info(&format!(
            "Waiting for boot signal (timeout: {boot_timeout}s)..."
        ));

        let signal = std::env::var("ZEN_BOOT_SIGNAL")
            .unwrap_or_else(|_| "ZEN_BOOT_OK".to_string());
        let fallback = "login:";
        let poll_interval = Duration::from_secs(1);
        let deadline = Duration::from_secs(boot_timeout);

        let result = serial::tail::wait_for_pattern(
            &paths.serial_log,
            &[&signal, fallback],
            &serial::patterns::ERROR_PATTERNS,
            deadline,
            poll_interval,
        )
        .await;

        match result {
            serial::tail::WaitResult::PatternFound(pat) => {
                boot_time_ms = Some(boot_start.elapsed().as_millis() as u64);
                if pat == fallback {
                    output::log_warn(&format!(
                        "Fallback boot signal detected: {fallback}"
                    ));
                } else {
                    output::log_ok(&format!(
                        "Boot signal detected: {pat} ({:.1}s)",
                        boot_start.elapsed().as_secs_f64()
                    ));
                }
            }
            serial::tail::WaitResult::ErrorFound(pat) => {
                return Err(ZenTestError::BootFailed(format!(
                    "Fatal error during boot: {pat}"
                )));
            }
            serial::tail::WaitResult::Timeout => {
                return Err(ZenTestError::BootTimeout(boot_timeout));
            }
        }
    }

    Ok(VmBootInfo {
        name: name.to_string(),
        pid,
        kvm_enabled: kvm,
        boot_time_ms,
    })
}

/// Stop a VM gracefully or forcefully.
pub async fn vm_stop(name: &str, force: bool, timeout_secs: u64) -> Result<()> {
    let paths = VmPaths::new(name);

    let pid = match paths.read_pid() {
        Some(pid) if Path::new(&format!("/proc/{pid}")).exists() => pid,
        _ => {
            output::log_info(&format!("VM not running: {name} (nothing to stop)"));
            return Ok(());
        }
    };

    if force {
        kill_process(pid);
        cleanup_pid_file(&paths);
        output::log_ok(&format!("VM force-killed: {name}"));
        return Ok(());
    }

    // Try graceful ACPI shutdown via QMP.
    if paths.qmp_socket.exists() {
        output::log_info("Sending ACPI powerdown via QMP...");
        let _ = crate::qmp::commands::qmp_system_powerdown(&paths.qmp_socket).await;
    }

    // Wait for process to exit.
    let deadline = Instant::now() + Duration::from_secs(timeout_secs);
    while Instant::now() < deadline {
        if !Path::new(&format!("/proc/{pid}")).exists() {
            cleanup_pid_file(&paths);
            output::log_ok(&format!("VM stopped gracefully: {name}"));
            return Ok(());
        }
        tokio::time::sleep(Duration::from_secs(1)).await;
    }

    // Graceful failed — try QMP quit.
    output::log_warn("Graceful shutdown timed out, sending QMP quit...");
    if paths.qmp_socket.exists() {
        let _ = crate::qmp::commands::qmp_quit(&paths.qmp_socket).await;
    }
    tokio::time::sleep(Duration::from_secs(1)).await;

    // Final resort: SIGKILL.
    if Path::new(&format!("/proc/{pid}")).exists() {
        kill_process(pid);
    }
    cleanup_pid_file(&paths);
    output::log_ok(&format!("VM stopped (forced): {name}"));

    Ok(())
}

/// Destroy a VM: stop + remove all files. Always succeeds.
pub async fn vm_destroy(name: &str) -> Result<()> {
    let paths = VmPaths::new(name);

    if !paths.exists() {
        output::log_info(&format!("VM does not exist: {name} (nothing to destroy)"));
        return Ok(());
    }

    // Stop if running.
    let _ = vm_stop(name, true, 5).await;

    // Remove all files.
    let _ = std::fs::remove_dir_all(&paths.dir);
    output::log_ok(&format!("VM destroyed: {name}"));

    Ok(())
}

/// List all VMs with status.
pub fn vm_list() -> Result<Vec<VmStatus>> {
    let vm_dir = std::env::var("ZEN_TEST_DIR")
        .unwrap_or_else(|_| DEFAULT_VM_DIR.to_string());

    let dir = Path::new(&vm_dir);
    if !dir.exists() {
        return Ok(vec![]);
    }

    let mut vms = Vec::new();
    for entry in std::fs::read_dir(dir)? {
        let entry = entry?;
        if !entry.file_type()?.is_dir() {
            continue;
        }
        let name = entry.file_name().to_string_lossy().to_string();
        let paths = VmPaths::new(&name);

        if !paths.config_file.exists() {
            continue;
        }

        let status = if paths.is_running() {
            "running".to_string()
        } else {
            "stopped".to_string()
        };

        vms.push(VmStatus {
            name,
            status,
            pid: paths.read_pid(),
        });
    }

    Ok(vms)
}

fn kill_process(pid: u32) {
    use nix::sys::signal::{kill, Signal};
    use nix::unistd::Pid;
    let _ = kill(Pid::from_raw(pid as i32), Signal::SIGKILL);
}

fn cleanup_pid_file(paths: &VmPaths) {
    let _ = std::fs::remove_file(&paths.pid_file);
}
