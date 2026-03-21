// zen-test — One-shot serial log scanner.
//
// Non-blocking: reads the file as-is and returns immediately.

use crate::serial::patterns::{PatternMatch, ERROR_PATTERNS};
use serde::Serialize;
use std::path::Path;

/// Result of a one-shot serial log scan.
#[derive(Debug, Serialize)]
pub struct ScanResult {
    pub boot_signal_found: bool,
    pub errors: Vec<PatternMatch>,
    pub line_count: usize,
}

/// Scan a serial log file for error patterns and boot signal.
pub fn scan_serial_log(log_path: &Path, boot_signal: &str) -> ScanResult {
    let content = match std::fs::read_to_string(log_path) {
        Ok(c) => c,
        Err(_) => {
            return ScanResult {
                boot_signal_found: false,
                errors: vec![],
                line_count: 0,
            };
        }
    };

    let mut errors = Vec::new();
    let mut boot_signal_found = false;
    let mut line_count = 0;

    for (i, line) in content.lines().enumerate() {
        line_count = i + 1;

        if line.contains(boot_signal) {
            boot_signal_found = true;
        }

        for ep in ERROR_PATTERNS {
            if line.contains(ep.pattern) {
                errors.push(PatternMatch {
                    pattern: ep.pattern.to_string(),
                    tag: ep.tag.to_string(),
                    line_number: i + 1,
                    line: line.to_string(),
                });
            }
        }
    }

    ScanResult {
        boot_signal_found,
        errors,
        line_count,
    }
}

/// Check if a serial log contains a specific pattern.
pub fn log_contains(log_path: &Path, pattern: &str) -> bool {
    std::fs::read_to_string(log_path)
        .map(|c| c.contains(pattern))
        .unwrap_or(false)
}

/// Check that a serial log does NOT contain a specific pattern.
pub fn log_absent(log_path: &Path, pattern: &str) -> bool {
    !log_contains(log_path, pattern)
}
