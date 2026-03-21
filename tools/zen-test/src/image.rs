// zen-test — Test image builder integration.

use crate::errors::{Result, ZenTestError};
use crate::output;
use serde::Serialize;
use std::path::Path;
use std::process::Command;

#[derive(Debug, Serialize)]
pub struct ImageInfo {
    pub path: String,
    pub size_bytes: u64,
}

/// Build a test image by invoking the build-test-image.sh script.
pub fn build_image(compositor_path: &Path, output_path: &Path) -> Result<ImageInfo> {
    // Find the build script relative to the zen-test binary.
    let script = find_build_script()?;

    output::log_info(&format!(
        "Building test image with compositor: {}",
        compositor_path.display()
    ));

    let status = Command::new("sudo")
        .args(["bash", &script.to_string_lossy()])
        .env("ZEN_COMPOSITOR", compositor_path)
        .env("ZEN_OUTPUT_IMAGE", output_path)
        .output()
        .map_err(|e| ZenTestError::ImageBuildFailed(format!("Failed to run build script: {e}")))?;

    if !status.status.success() {
        let stderr = String::from_utf8_lossy(&status.stderr);
        return Err(ZenTestError::ImageBuildFailed(format!(
            "Build script failed: {stderr}"
        )));
    }

    if !output_path.exists() {
        return Err(ZenTestError::ImageBuildFailed(
            "Build completed but output image not found".into(),
        ));
    }

    let metadata = std::fs::metadata(output_path)?;

    output::log_ok(&format!(
        "Image built: {} ({} MB)",
        output_path.display(),
        metadata.len() / 1024 / 1024
    ));

    Ok(ImageInfo {
        path: output_path.display().to_string(),
        size_bytes: metadata.len(),
    })
}

fn find_build_script() -> Result<std::path::PathBuf> {
    // Try relative paths from various locations.
    let candidates = [
        "tools/image-builder/build-test-image.sh",
        "../image-builder/build-test-image.sh",
        "../../tools/image-builder/build-test-image.sh",
    ];

    for candidate in &candidates {
        let path = Path::new(candidate);
        if path.exists() {
            return Ok(path.to_path_buf());
        }
    }

    // Try from repo root via git.
    if let Ok(output) = Command::new("git")
        .args(["rev-parse", "--show-toplevel"])
        .output()
    {
        if output.status.success() {
            let root = String::from_utf8_lossy(&output.stdout).trim().to_string();
            let path = Path::new(&root).join("tools/image-builder/build-test-image.sh");
            if path.exists() {
                return Ok(path);
            }
        }
    }

    Err(ZenTestError::InfraError(
        "build-test-image.sh not found. Run from the repo root.".into(),
    ))
}
