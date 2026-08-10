// zen-test — Offline guest image mutation helpers for gate pre_boot steps.

use crate::errors::{Result, ZenTestError};
use crate::gate::definition::ExecStep;
use crate::output;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const NBD_DEVICE_LIMIT: usize = 8;

pub fn run_pre_boot_exec(vm_name: &str, disk_path: &Path, steps: &[ExecStep]) -> Result<()> {
    if steps.is_empty() {
        return Ok(());
    }

    output::log_info(&format!(
        "Applying {} pre-boot step(s) to {}",
        steps.len(),
        disk_path.display()
    ));

    ensure_sudo_available()?;
    run_sudo_command(
        ["modprobe", "nbd", "max_part=8"],
        "load nbd kernel module",
    )?;

    let mount_dir = unique_mount_dir(vm_name);
    std::fs::create_dir_all(&mount_dir)?;

    let mut connected_nbd: Option<String> = None;
    let mut mounted_root = false;
    let result = (|| {
        let nbd = connect_nbd_device(disk_path)?;
        let root_block = wait_for_root_block_device(&nbd)?;

        connected_nbd = Some(nbd);

        run_sudo_command(
            [
                "mount",
                root_block.as_str(),
                path_as_str(&mount_dir)?,
            ],
            "mount offline guest root filesystem",
        )?;
        mounted_root = true;

        for step in steps {
            run_pre_boot_step(&mount_dir, step)?;
        }

        Ok(())
    })();

    if mounted_root {
        let _ = run_sudo_command(
            ["umount", path_as_str(&mount_dir).unwrap_or("/")],
            "unmount offline guest root filesystem",
        );
    }

    if let Some(ref nbd) = connected_nbd {
        let _ = run_sudo_command(
            ["qemu-nbd", "--disconnect", nbd.as_str()],
            "disconnect qemu-nbd device",
        );
    }

    let _ = std::fs::remove_dir_all(&mount_dir);
    result
}

fn ensure_sudo_available() -> Result<()> {
    let output = Command::new("sudo")
        .args(["-n", "true"])
        .output()
        .map_err(|e| {
            ZenTestError::InfraError(format!("Failed to invoke sudo for pre_boot.exec: {e}"))
        })?;

    if output.status.success() {
        return Ok(());
    }

    Err(ZenTestError::InfraError(
        "pre_boot.exec requires passwordless sudo for qemu-nbd and mount operations".into(),
    ))
}

fn run_pre_boot_step(mount_dir: &Path, step: &ExecStep) -> Result<()> {
    output::log_info(&format!("Pre-boot exec: {}", step.command));

    let timeout_seconds = step.timeout_seconds.to_string();
    let output = Command::new("sudo")
        .args(["-n", "timeout", timeout_seconds.as_str(), "chroot"])
        .arg(mount_dir)
        .args(["/bin/bash", "-lc", &step.command])
        .output()
        .map_err(|e| {
            ZenTestError::InfraError(format!(
                "Failed to execute pre_boot.exec command '{}': {e}",
                step.command
            ))
        })?;

    if output.status.success() {
        return Ok(());
    }

    if output.status.code() == Some(124) {
        return Err(ZenTestError::Timeout(step.timeout_seconds));
    }

    let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
    let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();

    Err(ZenTestError::GateFailed(format!(
        "pre_boot.exec failed: command={:?}, exit_code={:?}, stdout={:?}, stderr={:?}",
        step.command,
        output.status.code(),
        stdout,
        stderr
    )))
}

fn connect_nbd_device(disk_path: &Path) -> Result<String> {
    for index in 0..NBD_DEVICE_LIMIT {
        let device = format!("/dev/nbd{index}");
        let output = Command::new("sudo")
            .args(["-n", "qemu-nbd", "--connect", device.as_str()])
            .arg(disk_path)
            .output()
            .map_err(|e| {
                ZenTestError::InfraError(format!(
                    "Failed to invoke qemu-nbd for {}: {e}",
                    disk_path.display()
                ))
            })?;

        if output.status.success() {
            output::log_ok(&format!("Connected {} to {}", disk_path.display(), device));
            return Ok(device);
        }
    }

    Err(ZenTestError::InfraError(
        "Unable to find a free /dev/nbdX device for pre_boot.exec".into(),
    ))
}

fn wait_for_root_block_device(nbd_device: &str) -> Result<String> {
    let partition_path = partition_device_path(nbd_device);
    for _ in 0..40 {
        if Path::new(&partition_path).exists() {
            return Ok(partition_path);
        }
        thread::sleep(Duration::from_millis(100));
    }

    if Path::new(nbd_device).exists() {
        return Ok(nbd_device.to_string());
    }

    Err(ZenTestError::InfraError(format!(
        "No root block device appeared for {nbd_device} after qemu-nbd connect"
    )))
}

fn partition_device_path(nbd_device: &str) -> String {
    format!("{nbd_device}p1")
}

fn run_sudo_command<const N: usize>(args: [&str; N], description: &str) -> Result<()> {
    let output = Command::new("sudo")
        .arg("-n")
        .args(args)
        .output()
        .map_err(|e| {
            ZenTestError::InfraError(format!("Failed to {description}: {e}"))
        })?;

    if output.status.success() {
        return Ok(());
    }

    let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
    Err(ZenTestError::InfraError(format!(
        "Failed to {description}: {}",
        if stderr.is_empty() {
            format!("exit status {:?}", output.status.code())
        } else {
            stderr
        }
    )))
}

fn path_as_str(path: &Path) -> Result<&str> {
    path.to_str().ok_or_else(|| {
        ZenTestError::InfraError(format!(
            "Path is not valid UTF-8: {}",
            path.display()
        ))
    })
}

fn unique_mount_dir(vm_name: &str) -> PathBuf {
    let unique = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    std::env::temp_dir().join(format!("zen-test-preboot-{vm_name}-{unique}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn partition_device_path_uses_first_partition_suffix() {
        assert_eq!(partition_device_path("/dev/nbd3"), "/dev/nbd3p1");
    }

    #[test]
    fn unique_mount_dir_has_expected_prefix() {
        let mount_dir = unique_mount_dir("gate");
        let file_name = mount_dir.file_name().and_then(|name| name.to_str()).unwrap();
        assert!(file_name.starts_with("zen-test-preboot-gate-"));
    }
}
