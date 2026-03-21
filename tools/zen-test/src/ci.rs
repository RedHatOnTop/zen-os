// zen-test — CI integration helpers.

use std::env;

/// Check if running inside GitHub Actions.
pub fn is_ci() -> bool {
    env::var("GITHUB_ACTIONS").is_ok()
}

/// Emit a GitHub Actions error annotation.
pub fn gh_error(msg: &str) {
    if is_ci() {
        println!("::error::{msg}");
    }
}

/// Emit a GitHub Actions warning annotation.
pub fn gh_warning(msg: &str) {
    if is_ci() {
        println!("::warning::{msg}");
    }
}

/// Set a GitHub Actions output variable.
pub fn gh_set_output(name: &str, value: &str) {
    if is_ci() {
        if let Ok(path) = env::var("GITHUB_OUTPUT") {
            let _ = std::fs::write(&path, format!("{name}={value}\n"));
        }
    }
}
