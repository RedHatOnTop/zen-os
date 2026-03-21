// zen-test — VM configuration.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

/// Default VM storage directory.
pub const DEFAULT_VM_DIR: &str = "/tmp/zen-test-vms";

/// Default values.
pub const DEFAULT_RAM_MB: u32 = 2048;
pub const DEFAULT_CPUS: u32 = 2;
pub const DEFAULT_DISK_SIZE: &str = "32G";
pub const DEFAULT_DISPLAY: &str = "1920x1080";
pub const DEFAULT_BOOT_TIMEOUT: u64 = 120;
pub const DEFAULT_QEMU_BIN: &str = "qemu-system-x86_64";

/// Persisted VM configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VmConfig {
    pub name: String,
    pub ram_mb: u32,
    pub cpus: u32,
    pub disk_size: String,
    pub display: String,
    pub image: PathBuf,
    pub disk_path: PathBuf,
    pub created: String,
}

/// Runtime VM paths derived from a VM name.
#[derive(Debug, Clone)]
pub struct VmPaths {
    pub dir: PathBuf,
    pub config_file: PathBuf,
    pub disk: PathBuf,
    pub qmp_socket: PathBuf,
    pub agent_socket: PathBuf,
    pub serial_log: PathBuf,
    pub pid_file: PathBuf,
    pub qemu_stderr: PathBuf,
    pub screenshots_dir: PathBuf,
}

impl VmPaths {
    pub fn new(name: &str) -> Self {
        let vm_dir = std::env::var("ZEN_TEST_DIR").unwrap_or_else(|_| DEFAULT_VM_DIR.to_string());
        let dir = PathBuf::from(&vm_dir).join(name);
        Self {
            config_file: dir.join("config.json"),
            disk: dir.join("disk.qcow2"),
            qmp_socket: dir.join("qmp.sock"),
            agent_socket: dir.join("agent.sock"),
            serial_log: dir.join("serial.log"),
            pid_file: dir.join("qemu.pid"),
            qemu_stderr: dir.join("qemu-stderr.log"),
            screenshots_dir: dir.join("screenshots"),
            dir,
        }
    }

    /// Check if the VM directory exists.
    pub fn exists(&self) -> bool {
        self.dir.exists()
    }

    /// Read the PID from the pidfile, if it exists.
    pub fn read_pid(&self) -> Option<u32> {
        std::fs::read_to_string(&self.pid_file)
            .ok()
            .and_then(|s| s.trim().parse().ok())
    }

    /// Check if the VM process is running.
    pub fn is_running(&self) -> bool {
        if let Some(pid) = self.read_pid() {
            Path::new(&format!("/proc/{pid}")).exists()
        } else {
            false
        }
    }
}

/// Info returned after VM creation.
#[derive(Debug, Serialize)]
pub struct VmInfo {
    pub name: String,
    pub ram_mb: u32,
    pub cpus: u32,
    pub disk_path: String,
    pub image: String,
}

/// Info returned after VM boot.
#[derive(Debug, Serialize)]
pub struct VmBootInfo {
    pub name: String,
    pub pid: u32,
    pub kvm_enabled: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub boot_time_ms: Option<u64>,
}

/// VM status for listing.
#[derive(Debug, Serialize)]
pub struct VmStatus {
    pub name: String,
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pid: Option<u32>,
}
