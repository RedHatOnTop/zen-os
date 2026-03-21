// zen-test — QEMU command builder and process spawning.

use crate::errors::{Result, ZenTestError};
use crate::output;
use crate::vm::config::{VmPaths, DEFAULT_QEMU_BIN};
use std::path::Path;
use std::process::Command;

/// Check if KVM is available.
pub fn kvm_available() -> bool {
    Path::new("/dev/kvm").exists()
}

/// Check that the QEMU binary exists.
pub fn check_qemu_binary() -> Result<String> {
    let bin = std::env::var("ZEN_QEMU_BIN").unwrap_or_else(|_| DEFAULT_QEMU_BIN.to_string());

    let status = Command::new("which")
        .arg(&bin)
        .output()
        .map_err(|e| ZenTestError::InfraError(format!("Failed to check for {bin}: {e}")))?;

    if !status.status.success() {
        return Err(ZenTestError::InfraError(format!(
            "QEMU binary not found: {bin}. Install with: sudo apt install qemu-system-x86"
        )));
    }

    Ok(bin)
}

/// Build the QEMU command line arguments.
pub fn build_qemu_args(
    qemu_bin: &str,
    name: &str,
    ram_mb: u32,
    cpus: u32,
    disk_path: &Path,
    display: &str,
    paths: &VmPaths,
    headless: bool,
) -> Vec<String> {
    let width = display.split('x').next().unwrap_or("1920");
    let height = display.split('x').nth(1).unwrap_or("1080");

    let mut args = vec![
        qemu_bin.to_string(),
        "-name".into(),
        name.into(),
        "-m".into(),
        ram_mb.to_string(),
        "-smp".into(),
        cpus.to_string(),
        "-drive".into(),
        format!("file={},format=qcow2,if=virtio", disk_path.display()),
        "-qmp".into(),
        format!("unix:{},server,nowait", paths.qmp_socket.display()),
        "-serial".into(),
        format!("file:{}", paths.serial_log.display()),
        "-pidfile".into(),
        paths.pid_file.display().to_string(),
        "-daemonize".into(),
        // USB controller for hot-plug testing.
        "-device".into(),
        "qemu-xhci,id=usb-bus".into(),
        // Virtio-serial for guest agent communication.
        "-device".into(),
        "virtio-serial".into(),
        "-chardev".into(),
        format!(
            "socket,id=agent,path={},server=on,wait=off",
            paths.agent_socket.display()
        ),
        "-device".into(),
        "virtserialport,chardev=agent,name=org.zenos.agent".into(),
        // Networking.
        "-nic".into(),
        "user,model=virtio-net-pci".into(),
    ];

    // KVM acceleration.
    if kvm_available() {
        args.push("-enable-kvm".into());
    } else {
        output::log_warn("KVM not available, falling back to TCG (slow)");
    }

    // Display mode.
    if headless {
        args.extend_from_slice(&[
            "-device".into(),
            "virtio-vga".into(),
            "-display".into(),
            "none".into(),
        ]);
    } else {
        args.extend_from_slice(&[
            "-device".into(),
            format!("virtio-vga-gl,xres={width},yres={height}"),
            "-display".into(),
            "gtk,gl=on".into(),
        ]);
    }

    args
}

/// Spawn QEMU as a daemonized process. Returns immediately.
pub fn spawn_qemu(args: &[String], stderr_log: &Path) -> Result<()> {
    let qemu_bin = &args[0];
    let qemu_args = &args[1..];

    output::log_info(&format!("Spawning QEMU: {qemu_bin} (headless, daemonized)"));
    output::log_debug(&format!("QEMU args: {}", qemu_args.join(" ")));

    let stderr_file = std::fs::File::create(stderr_log).map_err(|e| ZenTestError::Io(e))?;

    let status = Command::new(qemu_bin)
        .args(qemu_args)
        .stderr(stderr_file)
        .output()
        .map_err(|e| ZenTestError::QemuStartFailed(format!("{e}")))?;

    if !status.status.success() {
        let stderr = std::fs::read_to_string(stderr_log).unwrap_or_default();
        return Err(ZenTestError::QemuStartFailed(format!(
            "QEMU exited with {}: {}",
            status.status,
            stderr.trim()
        )));
    }

    Ok(())
}

/// Create a qcow2 overlay disk backed by a base image.
pub fn create_overlay_disk(disk_path: &Path, base_image: &Path, size: &str) -> Result<()> {
    output::log_info(&format!(
        "Creating overlay disk: {} (backed by {})",
        disk_path.display(),
        base_image.display()
    ));

    let status = Command::new("qemu-img")
        .args(["create", "-f", "qcow2", "-b"])
        .arg(base_image)
        .args(["-F", "qcow2"])
        .arg(disk_path)
        .arg(size)
        .output()
        .map_err(|e| ZenTestError::InfraError(format!("qemu-img not found: {e}")))?;

    if !status.status.success() {
        let stderr = String::from_utf8_lossy(&status.stderr);
        return Err(ZenTestError::InfraError(format!(
            "qemu-img create failed: {stderr}"
        )));
    }

    Ok(())
}

/// Create a blank qcow2 disk (no base image).
pub fn create_blank_disk(disk_path: &Path, size: &str) -> Result<()> {
    output::log_info(&format!(
        "Creating blank disk: {} ({size})",
        disk_path.display()
    ));

    let status = Command::new("qemu-img")
        .args(["create", "-f", "qcow2"])
        .arg(disk_path)
        .arg(size)
        .output()
        .map_err(|e| ZenTestError::InfraError(format!("qemu-img not found: {e}")))?;

    if !status.status.success() {
        let stderr = String::from_utf8_lossy(&status.stderr);
        return Err(ZenTestError::InfraError(format!(
            "qemu-img create failed: {stderr}"
        )));
    }

    Ok(())
}
