// zen-test — QMP protocol client.
//
// Connect → negotiate → command → disconnect pattern.
// No persistent sessions. Each call is a one-shot operation with timeout.

use crate::errors::{Result, ZenTestError};
use crate::output;
use serde_json::Value;
use std::path::Path;
use std::time::Duration;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::UnixStream;

/// Default per-command timeout for QMP operations.
const QMP_TIMEOUT_SECS: u64 = 5;

/// Send a QMP command and return the response.
///
/// Opens a fresh connection, negotiates capabilities, sends the command,
/// reads the response, and drops the connection. Never blocks forever.
pub async fn qmp_execute(
    socket_path: &Path,
    command: &str,
    args: Option<Value>,
) -> Result<Value> {
    let result = tokio::time::timeout(
        Duration::from_secs(QMP_TIMEOUT_SECS),
        qmp_execute_inner(socket_path, command, args),
    )
    .await;

    match result {
        Ok(Ok(val)) => Ok(val),
        Ok(Err(e)) => Err(e),
        Err(_) => Err(ZenTestError::QmpError(format!(
            "QMP command '{command}' timed out after {QMP_TIMEOUT_SECS}s"
        ))),
    }
}

async fn qmp_execute_inner(
    socket_path: &Path,
    command: &str,
    args: Option<Value>,
) -> Result<Value> {
    if !socket_path.exists() {
        return Err(ZenTestError::QmpConnectionFailed(format!(
            "QMP socket does not exist: {}",
            socket_path.display()
        )));
    }

    // Connect.
    let stream = UnixStream::connect(socket_path).await.map_err(|e| {
        ZenTestError::QmpConnectionFailed(format!(
            "Failed to connect to {}: {e}",
            socket_path.display()
        ))
    })?;

    let (reader, mut writer) = stream.into_split();
    let mut reader = BufReader::new(reader);
    let mut line = String::new();

    // 1. Read QMP greeting.
    line.clear();
    reader.read_line(&mut line).await.map_err(|e| {
        ZenTestError::QmpError(format!("Failed to read QMP greeting: {e}"))
    })?;
    output::log_debug(&format!("QMP greeting: {}", line.trim()));

    // 2. Send qmp_capabilities.
    let caps = b"{\"execute\":\"qmp_capabilities\"}\n";
    writer.write_all(caps).await.map_err(|e| {
        ZenTestError::QmpError(format!("Failed to send qmp_capabilities: {e}"))
    })?;

    // 3. Read capabilities response.
    line.clear();
    reader.read_line(&mut line).await.map_err(|e| {
        ZenTestError::QmpError(format!("Failed to read caps response: {e}"))
    })?;
    output::log_debug(&format!("QMP caps response: {}", line.trim()));

    // 4. Send actual command.
    let cmd = if let Some(ref a) = args {
        serde_json::json!({"execute": command, "arguments": a})
    } else {
        serde_json::json!({"execute": command})
    };

    let cmd_str = format!("{}\n", serde_json::to_string(&cmd)?);
    writer.write_all(cmd_str.as_bytes()).await.map_err(|e| {
        ZenTestError::QmpError(format!("Failed to send command '{command}': {e}"))
    })?;

    // 5. Read response (may need to skip event lines).
    loop {
        line.clear();
        let bytes = reader.read_line(&mut line).await.map_err(|e| {
            ZenTestError::QmpError(format!("Failed to read response: {e}"))
        })?;

        if bytes == 0 {
            return Err(ZenTestError::QmpError(
                "QMP connection closed before response".into(),
            ));
        }

        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }

        // Parse JSON.
        let val: Value = serde_json::from_str(trimmed).map_err(|e| {
            ZenTestError::QmpError(format!("Invalid QMP JSON: {e}: {trimmed}"))
        })?;

        // Skip event lines (they have "event" key, not "return"/"error").
        if val.get("event").is_some() {
            output::log_debug(&format!("QMP event: {trimmed}"));
            continue;
        }

        // Check for error.
        if let Some(err) = val.get("error") {
            let desc = err
                .get("desc")
                .and_then(|d| d.as_str())
                .unwrap_or("unknown QMP error");
            return Err(ZenTestError::QmpError(format!(
                "QMP command '{command}' failed: {desc}"
            )));
        }

        return Ok(val);
    }
}
