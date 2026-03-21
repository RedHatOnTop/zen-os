// zen-test — Application-wide error types.

use thiserror::Error;

#[derive(Debug, Error)]
pub enum ZenTestError {
    #[error("VM not found: {0}")]
    VmNotFound(String),

    #[error("VM already exists: {0}")]
    VmAlreadyExists(String),

    #[error("VM not running: {0}")]
    VmNotRunning(String),

    #[error("VM already running: {0}")]
    VmAlreadyRunning(String),

    #[error("QEMU failed to start: {0}")]
    QemuStartFailed(String),

    #[error("QMP error: {0}")]
    QmpError(String),

    #[error("QMP connection failed: {0}")]
    QmpConnectionFailed(String),

    #[error("Guest agent error: {0}")]
    AgentError(String),

    #[error("Guest agent timeout after {0}s")]
    AgentTimeout(u64),

    #[error("Boot timeout after {0}s")]
    BootTimeout(u64),

    #[error("Boot failed: {0}")]
    BootFailed(String),

    #[error("Screenshot failed: {0}")]
    ScreenshotFailed(String),

    #[error("Gate parse error: {0}")]
    GateParseError(String),

    #[error("Gate failed: {0}")]
    GateFailed(String),

    #[error("Image build failed: {0}")]
    ImageBuildFailed(String),

    #[error("Infrastructure error: {0}")]
    InfraError(String),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),

    #[error("TOML parse error: {0}")]
    Toml(#[from] toml::de::Error),

    #[error("Timeout after {0}s")]
    Timeout(u64),
}

pub type Result<T> = std::result::Result<T, ZenTestError>;
