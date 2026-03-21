// zen-test — Typed QMP command wrappers.

use crate::errors::Result;
use crate::qmp::client::qmp_execute;
use serde_json::json;
use std::path::Path;

/// Capture the guest framebuffer to a PPM file.
pub async fn qmp_screendump(socket: &Path, output_path: &Path) -> Result<()> {
    qmp_execute(
        socket,
        "screendump",
        Some(json!({"filename": output_path.to_string_lossy()})),
    )
    .await?;
    Ok(())
}

/// Send ACPI power button (graceful shutdown request).
pub async fn qmp_system_powerdown(socket: &Path) -> Result<()> {
    qmp_execute(socket, "system_powerdown", None).await?;
    Ok(())
}

/// Hard-quit QEMU.
pub async fn qmp_quit(socket: &Path) -> Result<()> {
    // quit may close the connection before sending a response, so we
    // tolerate errors here.
    let _ = qmp_execute(socket, "quit", None).await;
    Ok(())
}

/// Pause (suspend) the VM.
pub async fn qmp_stop(socket: &Path) -> Result<()> {
    qmp_execute(socket, "stop", None).await?;
    Ok(())
}

/// Resume a paused VM.
pub async fn qmp_cont(socket: &Path) -> Result<()> {
    qmp_execute(socket, "cont", None).await?;
    Ok(())
}

/// Query VM status (running, paused, shutdown, etc.).
pub async fn qmp_query_status(socket: &Path) -> Result<String> {
    let resp = qmp_execute(socket, "query-status", None).await?;
    let status = resp
        .get("return")
        .and_then(|r| r.get("status"))
        .and_then(|s| s.as_str())
        .unwrap_or("unknown");
    Ok(status.to_string())
}

/// Send a key combination to the guest.
pub async fn qmp_send_key(socket: &Path, keys: &[&str]) -> Result<()> {
    let keys_json: Vec<serde_json::Value> = keys
        .iter()
        .map(|k| json!({"type": "qcode", "data": k}))
        .collect();

    qmp_execute(
        socket,
        "send-key",
        Some(json!({"keys": keys_json})),
    )
    .await?;
    Ok(())
}

/// Hot-add a USB storage device.
pub async fn qmp_add_usb_drive(
    socket: &Path,
    image: &Path,
    drive_id: &str,
) -> Result<()> {
    // Add block device.
    qmp_execute(
        socket,
        "blockdev-add",
        Some(json!({
            "driver": "qcow2",
            "node-name": drive_id,
            "file": {
                "driver": "file",
                "filename": image.to_string_lossy()
            }
        })),
    )
    .await?;

    // Add USB device.
    qmp_execute(
        socket,
        "device_add",
        Some(json!({
            "driver": "usb-storage",
            "bus": "usb-bus.0",
            "drive": drive_id,
            "id": format!("{drive_id}-dev")
        })),
    )
    .await?;

    Ok(())
}

/// Hot-remove a device.
pub async fn qmp_remove_device(socket: &Path, device_id: &str) -> Result<()> {
    qmp_execute(
        socket,
        "device_del",
        Some(json!({"id": device_id})),
    )
    .await?;
    Ok(())
}
