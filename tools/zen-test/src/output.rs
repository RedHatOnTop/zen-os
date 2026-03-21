// zen-test — Zen OS VM Testing Infrastructure
// Structured output module: JSON to stdout, JSONL logs to stderr.
//
// Design: LLM-agent-first. stdout = single JSON result. stderr = real-time log.

use chrono::Utc;
use serde::Serialize;
use std::process;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Instant;

/// Global flag: if true, suppress stderr log output.
static QUIET: AtomicBool = AtomicBool::new(false);

/// Exit codes per DESIGN.md.
pub const EXIT_OK: i32 = 0;
pub const EXIT_FAIL: i32 = 1;
pub const EXIT_TIMEOUT: i32 = 2;
pub const EXIT_INFRA: i32 = 3;
pub const EXIT_ARGS: i32 = 4;

/// The final JSON object emitted on stdout.
#[derive(Serialize)]
pub struct Output<T: Serialize> {
    pub status: &'static str,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<T>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    pub code: i32,
    pub elapsed_ms: u64,
}

/// A single log line emitted to stderr (JSONL format).
#[derive(Serialize)]
struct LogLine<'a> {
    ts: String,
    level: &'a str,
    msg: &'a str,
}

/// Set quiet mode (suppress stderr logs).
pub fn set_quiet(quiet: bool) {
    QUIET.store(quiet, Ordering::Relaxed);
}

/// Emit a structured log line to stderr.
fn log_line(level: &str, msg: &str) {
    if QUIET.load(Ordering::Relaxed) {
        return;
    }
    let line = LogLine {
        ts: Utc::now().to_rfc3339(),
        level,
        msg,
    };
    if let Ok(json) = serde_json::to_string(&line) {
        eprintln!("{json}");
    }
}

pub fn log_info(msg: &str) {
    log_line("info", msg);
}

pub fn log_warn(msg: &str) {
    log_line("warn", msg);
}

pub fn log_error(msg: &str) {
    log_line("error", msg);
}

pub fn log_ok(msg: &str) {
    log_line("ok", msg);
}

pub fn log_debug(msg: &str) {
    log_line("debug", msg);
}

/// Emit a success result to stdout and exit 0.
pub fn emit_ok<T: Serialize>(data: T, start: Instant) -> ! {
    let output = Output {
        status: "ok",
        data: Some(data),
        error: None,
        code: EXIT_OK,
        elapsed_ms: start.elapsed().as_millis() as u64,
    };
    println!("{}", serde_json::to_string(&output).unwrap_or_default());
    process::exit(EXIT_OK);
}

/// Emit a failure result to stdout and exit 1.
pub fn emit_fail(msg: &str, start: Instant) -> ! {
    let output: Output<()> = Output {
        status: "fail",
        data: None,
        error: Some(msg.to_string()),
        code: EXIT_FAIL,
        elapsed_ms: start.elapsed().as_millis() as u64,
    };
    println!("{}", serde_json::to_string(&output).unwrap_or_default());
    process::exit(EXIT_FAIL);
}

/// Emit a timeout result to stdout and exit 2.
pub fn emit_timeout(msg: &str, start: Instant) -> ! {
    let output: Output<()> = Output {
        status: "timeout",
        data: None,
        error: Some(msg.to_string()),
        code: EXIT_TIMEOUT,
        elapsed_ms: start.elapsed().as_millis() as u64,
    };
    println!("{}", serde_json::to_string(&output).unwrap_or_default());
    process::exit(EXIT_TIMEOUT);
}

/// Emit an infrastructure error to stdout and exit 3.
pub fn emit_infra_error(msg: &str, start: Instant) -> ! {
    let output: Output<()> = Output {
        status: "error",
        data: None,
        error: Some(msg.to_string()),
        code: EXIT_INFRA,
        elapsed_ms: start.elapsed().as_millis() as u64,
    };
    println!("{}", serde_json::to_string(&output).unwrap_or_default());
    process::exit(EXIT_INFRA);
}

/// Emit an argument error to stdout and exit 4.
pub fn emit_args_error(msg: &str) -> ! {
    let output: Output<()> = Output {
        status: "error",
        data: None,
        error: Some(msg.to_string()),
        code: EXIT_ARGS,
        elapsed_ms: 0,
    };
    println!("{}", serde_json::to_string(&output).unwrap_or_default());
    process::exit(EXIT_ARGS);
}

/// Non-exiting version: build a JSON result string.
pub fn build_ok_json<T: Serialize>(data: &T, start: Instant) -> String {
    let output = Output {
        status: "ok",
        data: Some(data),
        error: None,
        code: EXIT_OK,
        elapsed_ms: start.elapsed().as_millis() as u64,
    };
    serde_json::to_string(&output).unwrap_or_default()
}

pub fn build_fail_json(msg: &str, start: Instant) -> String {
    let output: Output<()> = Output {
        status: "fail",
        data: None,
        error: Some(msg.to_string()),
        code: EXIT_FAIL,
        elapsed_ms: start.elapsed().as_millis() as u64,
    };
    serde_json::to_string(&output).unwrap_or_default()
}

pub fn build_timeout_json(msg: &str, start: Instant) -> String {
    let output: Output<()> = Output {
        status: "timeout",
        data: None,
        error: Some(msg.to_string()),
        code: EXIT_TIMEOUT,
        elapsed_ms: start.elapsed().as_millis() as u64,
    };
    serde_json::to_string(&output).unwrap_or_default()
}
