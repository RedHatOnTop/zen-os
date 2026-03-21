// zen-test — Guest agent communication via virtio-serial.
//
// Connect → send JSON command → read response → disconnect.
// Each call is a one-shot operation with timeout.

use crate::errors::{Result, ZenTestError};
use crate::output;
use serde::{Deserialize, Serialize};
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixStream;

static NEXT_ID: AtomicU64 = AtomicU64::new(1);

/// Result of a guest command execution.
#[derive(Debug, Serialize, Deserialize)]
pub struct GuestExecResult {
    pub exit_code: i32,
    pub stdout: String,
    pub stderr: String,
}

/// Request sent to the guest agent.
#[derive(Serialize)]
struct AgentRequest {
    id: u64,
    cmd: &'static str,
    args: AgentExecArgs,
}

#[derive(Serialize)]
struct AgentExecArgs {
    command: String,
    timeout: u64,
}

/// Response from the guest agent.
#[derive(Deserialize)]
struct AgentResponse {
    #[allow(dead_code)]
    id: u64,
    status: String,
    #[serde(default)]
    exit_code: i32,
    #[serde(default)]
    stdout: String,
    #[serde(default)]
    stderr: String,
    #[serde(default)]
    message: String,
}

/// Execute a command inside the guest via virtio-serial agent.
///
/// Connects to the agent socket, sends a JSON command, reads the
/// JSON response, and disconnects. Never blocks longer than timeout.
pub async fn guest_exec(
    agent_socket: &Path,
    command: &str,
    timeout_secs: u64,
) -> Result<GuestExecResult> {
    let result = tokio::time::timeout(
        Duration::from_secs(timeout_secs + 2), // +2s for connection overhead
        guest_exec_inner(agent_socket, command, timeout_secs),
    )
    .await;

    match result {
        Ok(Ok(r)) => Ok(r),
        Ok(Err(e)) => Err(e),
        Err(_) => Err(ZenTestError::AgentTimeout(timeout_secs)),
    }
}

async fn guest_exec_inner(
    agent_socket: &Path,
    command: &str,
    timeout_secs: u64,
) -> Result<GuestExecResult> {
    if !agent_socket.exists() {
        return Err(ZenTestError::AgentError(format!(
            "Agent socket not found: {}. Is zen-test-agent running in the guest?",
            agent_socket.display()
        )));
    }

    output::log_info(&format!("Guest exec: {command}"));

    // Connect to agent socket.
    let stream = UnixStream::connect(agent_socket).await.map_err(|e| {
        ZenTestError::AgentError(format!(
            "Failed to connect to agent socket: {e}"
        ))
    })?;

    let (reader, mut writer) = stream.into_split();
    let mut reader = BufReader::new(reader);

    // Send command.
    let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    let request = AgentRequest {
        id,
        cmd: "exec",
        args: AgentExecArgs {
            command: command.to_string(),
            timeout: timeout_secs,
        },
    };

    let request_json = format!("{}\n", serde_json::to_string(&request)?);
    writer.write_all(request_json.as_bytes()).await.map_err(|e| {
        ZenTestError::AgentError(format!("Failed to send command: {e}"))
    })?;

    // Read response.
    let mut line = String::new();
    reader.read_line(&mut line).await.map_err(|e| {
        ZenTestError::AgentError(format!("Failed to read response: {e}"))
    })?;

    let resp: AgentResponse = serde_json::from_str(line.trim()).map_err(|e| {
        ZenTestError::AgentError(format!("Invalid agent response: {e}: {}", line.trim()))
    })?;

    if resp.status != "ok" {
        return Err(ZenTestError::AgentError(format!(
            "Agent error: {}",
            resp.message
        )));
    }

    Ok(GuestExecResult {
        exit_code: resp.exit_code,
        stdout: resp.stdout,
        stderr: resp.stderr,
    })
}

pub mod exec {
    pub use super::{guest_exec, GuestExecResult};
}
