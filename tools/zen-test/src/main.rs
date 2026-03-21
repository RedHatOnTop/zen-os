// zen-test — Zen OS VM Testing Infrastructure
//
// LLM-agent-first CLI. Every command is non-interactive, fire-and-forget.
// stdout = single JSON result. stderr = real-time JSONL log.
// See DESIGN.md for full specification.

mod agent;
mod ci;
mod errors;
mod gate;
mod image;
mod output;
mod qmp;
mod screenshot;
mod serial;
mod timeout;
mod vm;

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::time::Instant;

#[derive(Parser)]
#[command(
    name = "zen-test",
    about = "Zen OS VM Testing Infrastructure — LLM-agent-friendly",
    version
)]
struct Cli {
    #[command(subcommand)]
    command: Commands,

    /// Global timeout in seconds (0 = no timeout).
    #[arg(long, default_value = "120", global = true)]
    timeout: u64,

    /// Log level for stderr (debug, info, warn, error).
    #[arg(long, default_value = "info", global = true)]
    log_level: String,
}

#[derive(Subcommand)]
enum Commands {
    /// VM lifecycle management.
    Vm {
        #[command(subcommand)]
        action: VmAction,
    },
    /// Quality gate validation.
    Gate {
        #[command(subcommand)]
        action: GateAction,
    },
    /// Test image management.
    Image {
        #[command(subcommand)]
        action: ImageAction,
    },
    /// Show version information.
    Version,
}

#[derive(Subcommand)]
enum VmAction {
    /// Create a new VM.
    Create {
        name: String,
        #[arg(long, default_value = "2048")]
        ram: u32,
        #[arg(long, default_value = "2")]
        cpus: u32,
        #[arg(long, default_value = "32G")]
        disk: String,
        #[arg(long)]
        image: Option<PathBuf>,
    },
    /// Boot a VM (spawn QEMU).
    Boot {
        name: String,
        /// Wait for boot signal before returning.
        #[arg(long)]
        wait_boot: bool,
        /// Boot timeout override (seconds).
        #[arg(long, default_value = "120")]
        timeout: u64,
    },
    /// Stop a VM.
    Stop {
        name: String,
        /// Force-kill (SIGKILL, no graceful shutdown).
        #[arg(long)]
        force: bool,
        /// Shutdown timeout (seconds).
        #[arg(long, default_value = "15")]
        timeout: u64,
    },
    /// Destroy a VM (stop + remove all files). Always succeeds.
    Destroy { name: String },
    /// List all VMs.
    List,
    /// Execute a command inside the guest.
    Exec {
        name: String,
        command: String,
        /// Command timeout (seconds).
        #[arg(long, default_value = "10")]
        timeout: u64,
    },
    /// Take a screenshot.
    Screenshot {
        name: String,
        /// Output file path.
        #[arg(long)]
        output: Option<PathBuf>,
    },
    /// Scan serial log for errors/boot signal.
    SerialScan { name: String },
    /// Send a key combination to the guest.
    SendKey {
        name: String,
        /// Key names (e.g., "ctrl", "alt", "t").
        keys: Vec<String>,
    },
}

#[derive(Subcommand)]
enum GateAction {
    /// Run quality gate(s).
    Run {
        /// Specific gate TOML file to run.
        gate_file: Option<PathBuf>,
        /// Run all gates for a specific phase.
        #[arg(long)]
        phase: Option<String>,
        /// Base disk image for gate VMs.
        #[arg(long)]
        image: PathBuf,
        /// Gate directory (default: gates/ relative to zen-test).
        #[arg(long)]
        gate_dir: Option<PathBuf>,
        /// Also write TAP output to this file.
        #[arg(long)]
        tap: Option<PathBuf>,
        /// Write structured JSON results to this file.
        #[arg(long)]
        report_json: Option<PathBuf>,
        /// Copy screenshots (PPM) to this directory before VM cleanup.
        #[arg(long)]
        screenshots_dir: Option<PathBuf>,
    },
    /// List available gates.
    List {
        #[arg(long)]
        phase: Option<String>,
        #[arg(long)]
        gate_dir: Option<PathBuf>,
    },
    /// Validate a gate TOML file (parse only, no execution).
    Validate { gate_file: PathBuf },
}

#[derive(Subcommand)]
enum ImageAction {
    /// Build a test image.
    Build {
        /// Path to the compositor binary.
        #[arg(long)]
        compositor: PathBuf,
        /// Output image path.
        #[arg(long, default_value = "/tmp/zen-os-test.qcow2")]
        output: PathBuf,
    },
}

#[tokio::main]
async fn main() {
    let cli = Cli::parse();
    let start = Instant::now();

    // Configure log level.
    if cli.log_level == "quiet" {
        output::set_quiet(true);
    }

    match cli.command {
        Commands::Version => {
            output::emit_ok(
                serde_json::json!({
                    "name": "zen-test",
                    "version": env!("CARGO_PKG_VERSION"),
                }),
                start,
            );
        }

        Commands::Vm { action } => handle_vm(action, start).await,
        Commands::Gate { action } => handle_gate(action, start).await,
        Commands::Image { action } => handle_image(action, start),
    }
}

async fn handle_vm(action: VmAction, start: Instant) {
    match action {
        VmAction::Create { name, ram, cpus, disk, image } => {
            match vm::lifecycle::vm_create(
                &name,
                ram,
                cpus,
                &disk,
                vm::config::DEFAULT_DISPLAY,
                image.as_deref(),
            ) {
                Ok(info) => output::emit_ok(info, start),
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }

        VmAction::Boot { name, wait_boot, timeout } => {
            match vm::lifecycle::vm_boot(&name, wait_boot, timeout).await {
                Ok(info) => output::emit_ok(info, start),
                Err(errors::ZenTestError::BootTimeout(t)) => {
                    output::emit_timeout(&format!("Boot timed out after {t}s"), start)
                }
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }

        VmAction::Stop { name, force, timeout } => {
            match vm::lifecycle::vm_stop(&name, force, timeout).await {
                Ok(()) => output::emit_ok(
                    serde_json::json!({"vm": name, "stopped": true}),
                    start,
                ),
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }

        VmAction::Destroy { name } => {
            match vm::lifecycle::vm_destroy(&name).await {
                Ok(()) => output::emit_ok(
                    serde_json::json!({"vm": name, "destroyed": true}),
                    start,
                ),
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }

        VmAction::List => {
            match vm::lifecycle::vm_list() {
                Ok(vms) => output::emit_ok(vms, start),
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }

        VmAction::Exec { name, command, timeout } => {
            let paths = vm::config::VmPaths::new(&name);
            if !paths.is_running() {
                output::emit_fail(&format!("VM not running: {name}"), start);
            }
            match agent::guest_exec(&paths.agent_socket, &command, timeout).await {
                Ok(result) => output::emit_ok(result, start),
                Err(errors::ZenTestError::AgentTimeout(t)) => {
                    output::emit_timeout(&format!("Guest exec timed out after {t}s"), start)
                }
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }

        VmAction::Screenshot { name, output: out_path } => {
            let paths = vm::config::VmPaths::new(&name);
            let screenshot_path = out_path.unwrap_or_else(|| {
                paths.screenshots_dir.join("screenshot.ppm")
            });
            match screenshot::capture(&paths.qmp_socket, &screenshot_path).await {
                Ok(info) => output::emit_ok(info, start),
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }

        VmAction::SerialScan { name } => {
            let paths = vm::config::VmPaths::new(&name);
            let signal = std::env::var("ZEN_BOOT_SIGNAL")
                .unwrap_or_else(|_| "ZEN_BOOT_OK".to_string());
            let result = serial::scan::scan_serial_log(&paths.serial_log, &signal);
            output::emit_ok(result, start);
        }

        VmAction::SendKey { name, keys } => {
            let paths = vm::config::VmPaths::new(&name);
            let key_refs: Vec<&str> = keys.iter().map(|s| s.as_str()).collect();
            match qmp::commands::qmp_send_key(&paths.qmp_socket, &key_refs).await {
                Ok(()) => output::emit_ok(
                    serde_json::json!({"vm": name, "keys_sent": keys}),
                    start,
                ),
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }
    }
}

async fn handle_gate(action: GateAction, start: Instant) {
    match action {
        GateAction::Run { gate_file, phase, image, gate_dir, tap, report_json, screenshots_dir } => {
            let gates_dir = gate_dir.unwrap_or_else(|| {
                // Default: gates/ relative to the zen-test binary or CWD.
                PathBuf::from("gates")
            });

            let screenshots_out = screenshots_dir.as_deref();

            let results = if let Some(ref file) = gate_file {
                // Run a single gate.
                match gate::definition::parse_gate_file(file) {
                    Ok(spec) => {
                        let result = gate::runner::run_gate(&spec, &image, screenshots_out).await;
                        vec![result]
                    }
                    Err(e) => {
                        output::emit_fail(&format!("Failed to parse gate: {e}"), start);
                    }
                }
            } else {
                // Run all gates matching filters.
                gate::runner::run_gates(
                    &gates_dir,
                    phase.as_deref(),
                    &image,
                    screenshots_out,
                )
                .await
            };

            // Write TAP output if requested.
            if let Some(ref tap_path) = tap {
                let tap_content = gate::report::to_tap(&results);
                if let Err(e) = std::fs::write(tap_path, &tap_content) {
                    output::log_error(&format!("Failed to write TAP file: {e}"));
                }
            }

            let summary = gate::report::summary(&results);
            output::log_info(&summary);

            // Write structured JSON results if requested.
            if let Some(ref json_path) = report_json {
                let all_ok = results.iter().all(|r| r.status == "passed");
                let json_output = serde_json::json!({
                    "status": if all_ok { "ok" } else { "fail" },
                    "code": if all_ok { 0 } else { 1 },
                    "elapsed_ms": start.elapsed().as_millis() as u64,
                    "data": {
                        "summary": &summary,
                        "results": &results,
                    }
                });
                match serde_json::to_string_pretty(&json_output) {
                    Ok(content) => {
                        if let Some(parent) = json_path.parent() {
                            let _ = std::fs::create_dir_all(parent);
                        }
                        if let Err(e) = std::fs::write(json_path, &content) {
                            output::log_error(&format!("Failed to write report JSON: {e}"));
                        } else {
                            output::log_info(&format!("Report JSON written to {}", json_path.display()));
                        }
                    }
                    Err(e) => output::log_error(&format!("Failed to serialize report JSON: {e}")),
                }
            }

            // Determine overall exit status.
            let all_passed = results.iter().all(|r| r.status == "passed");
            if all_passed {
                output::emit_ok(
                    serde_json::json!({
                        "summary": summary,
                        "results": results,
                    }),
                    start,
                );
            } else {
                // Use emit_fail for failed gates so exit code is 1.
                let json = output::build_fail_json(&summary, start);
                // But we also want the results, so print custom JSON.
                let output = serde_json::json!({
                    "status": "fail",
                    "error": summary,
                    "data": {"results": results},
                    "code": 1,
                    "elapsed_ms": start.elapsed().as_millis() as u64,
                });
                println!("{}", serde_json::to_string(&output).unwrap_or(json));
                std::process::exit(1);
            }
        }

        GateAction::List { phase, gate_dir } => {
            let gates_dir = gate_dir.unwrap_or_else(|| PathBuf::from("gates"));
            let files = walk_toml_files_simple(&gates_dir);
            let mut gates = Vec::new();

            for file in &files {
                if let Ok(spec) = gate::definition::parse_gate_file(file) {
                    if let Some(ref p) = phase {
                        if spec.gate.phase != *p {
                            continue;
                        }
                    }
                    gates.push(serde_json::json!({
                        "name": spec.gate.name,
                        "phase": spec.gate.phase,
                        "sub_phase": spec.gate.sub_phase,
                        "file": file.display().to_string(),
                    }));
                }
            }

            output::emit_ok(gates, start);
        }

        GateAction::Validate { gate_file } => {
            match gate::definition::parse_gate_file(&gate_file) {
                Ok(spec) => {
                    output::emit_ok(
                        serde_json::json!({
                            "valid": true,
                            "name": spec.gate.name,
                            "assertions": {
                                "serial": spec.assert.serial.len(),
                                "serial_absent": spec.assert.serial_absent.len(),
                                "exec": spec.assert.exec.len(),
                                "screenshot": spec.assert.screenshot.len(),
                            }
                        }),
                        start,
                    );
                }
                Err(e) => {
                    output::emit_fail(&format!("Invalid gate file: {e}"), start);
                }
            }
        }
    }
}

fn handle_image(action: ImageAction, start: Instant) {
    match action {
        ImageAction::Build { compositor, output: out } => {
            match image::build_image(&compositor, &out) {
                Ok(info) => output::emit_ok(info, start),
                Err(e) => output::emit_fail(&e.to_string(), start),
            }
        }
    }
}

fn walk_toml_files_simple(dir: &std::path::Path) -> Vec<PathBuf> {
    let mut files = Vec::new();
    if let Ok(entries) = std::fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                files.extend(walk_toml_files_simple(&path));
            } else if path.extension().and_then(|e| e.to_str()) == Some("toml") {
                files.push(path);
            }
        }
    }
    files
}
