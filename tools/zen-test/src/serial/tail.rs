// zen-test — Poll-based serial log tail with deadline.
//
// Used for boot-wait: polls the serial log file for a pattern,
// checking for error patterns in parallel. Uses file read + seek,
// not inotify (works on /mnt/c WSL2 mounts).

use crate::output;
use crate::serial::patterns::ErrorPattern;
use std::path::Path;
use std::time::{Duration, Instant};

/// Result of waiting for a pattern.
#[derive(Debug)]
pub enum WaitResult {
    /// A success pattern was found.
    PatternFound(String),
    /// An error pattern was found (fatal).
    ErrorFound(String),
    /// Timeout expired.
    Timeout,
}

/// Poll a log file for any of the given patterns.
///
/// Returns as soon as a success or error pattern is found, or the deadline
/// expires. Uses polling (periodic file re-read) — no inotify dependency.
pub async fn wait_for_pattern(
    log_path: &Path,
    success_patterns: &[&str],
    error_patterns: &[ErrorPattern],
    deadline: Duration,
    poll_interval: Duration,
) -> WaitResult {
    let start = Instant::now();
    let mut last_offset: usize = 0;

    loop {
        if start.elapsed() >= deadline {
            return WaitResult::Timeout;
        }

        // Read the file content from where we left off.
        let content = match std::fs::read_to_string(log_path) {
            Ok(c) => c,
            Err(_) => {
                tokio::time::sleep(poll_interval).await;
                continue;
            }
        };

        // Only check new content since last read.
        let new_content = if last_offset < content.len() {
            &content[last_offset..]
        } else {
            ""
        };
        last_offset = content.len();

        if new_content.is_empty() {
            tokio::time::sleep(poll_interval).await;
            continue;
        }

        // Stream new lines to stderr log for real-time visibility.
        for line in new_content.lines() {
            let trimmed = line.trim();
            if !trimmed.is_empty() {
                output::log_debug(&format!("Serial: {trimmed}"));
            }
        }

        // Check for success patterns (search entire content, not just new).
        for pattern in success_patterns {
            if content.contains(pattern) {
                return WaitResult::PatternFound(pattern.to_string());
            }
        }

        // Check for error patterns.
        for ep in error_patterns {
            if content.contains(ep.pattern) {
                return WaitResult::ErrorFound(ep.pattern.to_string());
            }
        }

        tokio::time::sleep(poll_interval).await;
    }
}
