// zen-test — Screenshot capture and analysis.

use crate::errors::{Result, ZenTestError};
use crate::output;
use crate::qmp::commands::qmp_screendump;
use serde::Serialize;
use std::path::Path;

/// Screenshot metadata.
#[derive(Debug, Serialize)]
pub struct ScreenshotInfo {
    pub path: String,
    pub size_bytes: u64,
    pub is_blank: bool,
}

/// Capture a screenshot via QMP screendump.
///
/// Writes to a temp file first, then renames to the target path (atomic).
pub async fn capture(
    qmp_socket: &Path,
    output_path: &Path,
) -> Result<ScreenshotInfo> {
    // Ensure parent directory exists.
    if let Some(parent) = output_path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    // QMP screendump writes directly to the path.
    // We write to a .tmp first for atomicity.
    let tmp_path = output_path.with_extension("ppm.tmp");

    qmp_screendump(qmp_socket, &tmp_path).await.map_err(|e| {
        ZenTestError::ScreenshotFailed(format!("QMP screendump failed: {e}"))
    })?;

    // Brief wait for QEMU to write the file.
    tokio::time::sleep(std::time::Duration::from_millis(500)).await;

    if !tmp_path.exists() {
        return Err(ZenTestError::ScreenshotFailed(
            "Screenshot file not created by QEMU".into(),
        ));
    }

    // Rename to final path.
    std::fs::rename(&tmp_path, output_path)?;

    let metadata = std::fs::metadata(output_path)?;
    let is_blank = check_blank(output_path);

    output::log_ok(&format!(
        "Screenshot saved: {} ({} bytes, blank: {is_blank})",
        output_path.display(),
        metadata.len()
    ));

    Ok(ScreenshotInfo {
        path: output_path.display().to_string(),
        size_bytes: metadata.len(),
        is_blank,
    })
}

/// Check if a PPM image is blank (all pixels identical).
///
/// PPM format: "P6\n<width> <height>\n<max>\n<raw RGB pixels>"
/// We read a sample of pixels and check if they're all the same.
fn check_blank(ppm_path: &Path) -> bool {
    let data = match std::fs::read(ppm_path) {
        Ok(d) => d,
        Err(_) => return true,
    };

    // Find the start of pixel data (after 3 newlines in P6 format).
    let mut newlines = 0;
    let mut pixel_start = 0;
    for (i, &byte) in data.iter().enumerate() {
        if byte == b'\n' {
            newlines += 1;
            if newlines == 3 {
                pixel_start = i + 1;
                break;
            }
        }
    }

    if pixel_start >= data.len() || data.len() - pixel_start < 12 {
        return true; // Too small to analyze.
    }

    let pixels = &data[pixel_start..];
    if pixels.len() < 6 {
        return true;
    }

    // Sample: check first pixel's RGB against every 1000th pixel.
    let first_r = pixels[0];
    let first_g = pixels[1];
    let first_b = pixels[2];

    let step = std::cmp::max(3000, 3); // Check every 1000th pixel (3 bytes each)
    for chunk in pixels.chunks(step) {
        if chunk.len() < 3 {
            break;
        }
        if chunk[0] != first_r || chunk[1] != first_g || chunk[2] != first_b {
            return false; // Found a different pixel — not blank.
        }
    }

    true // All sampled pixels identical.
}
