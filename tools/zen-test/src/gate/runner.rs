// zen-test — Quality gate execution engine.
//
// Orchestrates: boot → setup → test → assert → cleanup.
// Every step has its own timeout and failure handling.

use crate::agent;
use crate::errors::{Result, ZenTestError};
use crate::gate::definition::GateSpec;
use crate::output;
use crate::screenshot;
use crate::serial;
use crate::vm::config::VmPaths;
use crate::vm::lifecycle;
use serde::Serialize;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

/// Overall gate result.
#[derive(Debug, Serialize)]
pub struct GateResult {
    pub gate: String,
    pub phase: String,
    pub sub_phase: String,
    pub status: String, // "passed", "failed", "timeout", "error"
    pub assertions: Vec<AssertionResult>,
    pub passed: usize,
    pub failed: usize,
    pub elapsed_ms: u64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub screenshots: Vec<String>,
}

/// Individual assertion result.
#[derive(Debug, Serialize)]
pub struct AssertionResult {
    pub description: String,
    pub passed: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
}

/// Run a single quality gate.
pub async fn run_gate(spec: &GateSpec, image: &Path, screenshots_out: Option<&Path>) -> GateResult {
    let start = Instant::now();
    let gate_name = &spec.gate.name;
    let vm_name = format!("gate-{}-{}", spec.gate.phase, spec.gate.sub_phase);

    output::log_info(&format!("═══ Gate: {gate_name} ═══"));

    // Wrap the entire gate execution in a timeout.
    let result = tokio::time::timeout(
        Duration::from_secs(spec.gate.timeout_seconds),
        run_gate_inner(spec, image, &vm_name, screenshots_out),
    )
    .await;

    // Always clean up the VM.
    let _ = lifecycle::vm_destroy(&vm_name).await;

    match result {
        Ok(Ok(gate_result)) => gate_result,
        Ok(Err(e)) => GateResult {
            gate: gate_name.clone(),
            phase: spec.gate.phase.clone(),
            sub_phase: spec.gate.sub_phase.clone(),
            status: "error".into(),
            assertions: vec![],
            passed: 0,
            failed: 0,
            elapsed_ms: start.elapsed().as_millis() as u64,
            error: Some(e.to_string()),
            screenshots: vec![],
        },
        Err(_) => GateResult {
            gate: gate_name.clone(),
            phase: spec.gate.phase.clone(),
            sub_phase: spec.gate.sub_phase.clone(),
            status: "timeout".into(),
            assertions: vec![],
            passed: 0,
            failed: 0,
            elapsed_ms: start.elapsed().as_millis() as u64,
            error: Some(format!(
                "Gate timed out after {}s",
                spec.gate.timeout_seconds
            )),
            screenshots: vec![],
        },
    }
}

async fn run_gate_inner(
    spec: &GateSpec,
    image: &Path,
    vm_name: &str,
    screenshots_out: Option<&Path>,
) -> Result<GateResult> {
    let start = Instant::now();
    let vm_spec = spec.vm.as_ref();
    let ram = vm_spec.map(|v| v.ram_mb).unwrap_or(2048);
    let cpus = vm_spec.map(|v| v.cpus).unwrap_or(2);
    let paths = VmPaths::new(vm_name);

    // 1. Create VM.
    lifecycle::vm_create(vm_name, ram, cpus, "32G", "1920x1080", Some(image))?;

    // 2. Boot VM and wait for boot signal.
    lifecycle::vm_boot(vm_name, true, 120).await?;

    // 2b. If the gate has exec assertions (assert.exec, setup, or test),
    //     OR if any serial assertion checks for ZEN_BOOT_OK,
    //     ensure ZEN_BOOT_OK is in the serial log before proceeding.
    //     The boot-wait may have accepted the "login:" fallback before
    //     the compositor finished initializing.
    let has_exec = !spec.assert.exec.is_empty()
        || spec.setup.is_some()
        || spec.test.is_some();

    let boot_signal = std::env::var("ZEN_BOOT_SIGNAL")
        .unwrap_or_else(|_| "ZEN_BOOT_OK".to_string());

    let needs_boot_signal = has_exec
        || spec.assert.serial.iter().any(|s| s.pattern == boot_signal);

    if needs_boot_signal {
        // Wait up to 60s for the boot signal to appear in serial log.
        let mut found = false;
        for _ in 0..60 {
            if serial::scan::log_contains(&paths.serial_log, &boot_signal) {
                found = true;
                break;
            }
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
        if found {
            output::log_ok("Boot signal confirmed in serial log");
        } else {
            output::log_warn("Boot signal not found after 60s — proceeding anyway");
        }

        // Wait for guest agent to become responsive (up to 30s) — only needed for exec.
        if has_exec {
            output::log_info("Waiting for guest agent...");
            let mut agent_ready = false;
            for attempt in 1..=15 {
                match agent::guest_exec(&paths.agent_socket, "echo ready", 3).await {
                    Ok(r) if r.exit_code == 0 => {
                        output::log_ok(&format!(
                            "Guest agent ready (attempt {attempt})"
                        ));
                        agent_ready = true;
                        break;
                    }
                    _ => {
                        tokio::time::sleep(Duration::from_secs(2)).await;
                    }
                }
            }
            if !agent_ready {
                output::log_warn("Guest agent not responsive after 30s — exec assertions may fail");
            }
        }
    }

    // 3. Run setup commands.
    if let Some(ref setup) = spec.setup {
        for step in &setup.exec {
            output::log_info(&format!("Setup: {}", step.command));
            let _ = agent::guest_exec(
                &paths.agent_socket,
                &step.command,
                step.timeout_seconds,
            )
            .await;
            // Setup failures are non-fatal (best effort).
        }
    }

    // 4. Run test commands.
    if let Some(ref test) = spec.test {
        for step in &test.exec {
            output::log_info(&format!("Test exec: {}", step.command));
            agent::guest_exec(
                &paths.agent_socket,
                &step.command,
                step.timeout_seconds,
            )
            .await?;
        }

        for loop_step in &test.exec_loop {
            for i in 1..=loop_step.count {
                output::log_info(&format!(
                    "Test loop [{}/{}]: {}",
                    i, loop_step.count, loop_step.command
                ));
                let _ = agent::guest_exec(
                    &paths.agent_socket,
                    &loop_step.command,
                    loop_step.timeout_seconds,
                )
                .await;
            }
        }
    }

    // 5. Evaluate assertions.
    let mut assertions = Vec::new();

    // Serial assertions.
    for sa in &spec.assert.serial {
        let found = serial::scan::log_contains(&paths.serial_log, &sa.pattern);
        assertions.push(AssertionResult {
            description: sa
                .description
                .clone()
                .unwrap_or_else(|| format!("Serial contains: {}", sa.pattern)),
            passed: found,
            detail: if found {
                None
            } else {
                Some(format!("Pattern not found: {}", sa.pattern))
            },
        });
    }

    // Serial-absent assertions.
    for sa in &spec.assert.serial_absent {
        let absent = serial::scan::log_absent(&paths.serial_log, &sa.pattern);
        assertions.push(AssertionResult {
            description: sa
                .description
                .clone()
                .unwrap_or_else(|| format!("Serial absent: {}", sa.pattern)),
            passed: absent,
            detail: if absent {
                None
            } else {
                Some(format!("Pattern found (should be absent): {}", sa.pattern))
            },
        });
    }

    // Exec assertions.
    for ea in &spec.assert.exec {
        let result = agent::guest_exec(
            &paths.agent_socket,
            &ea.command,
            ea.timeout_seconds,
        )
        .await;

        let (passed, detail) = match result {
            Ok(r) => {
                let ok = r.exit_code == ea.exit_code;
                let d = if ok {
                    None
                } else {
                    Some(format!(
                        "Expected exit code {}, got {}. stdout: {}",
                        ea.exit_code, r.exit_code, r.stdout.trim()
                    ))
                };
                (ok, d)
            }
            Err(e) => (false, Some(format!("Exec failed: {e}"))),
        };

        assertions.push(AssertionResult {
            description: ea
                .description
                .clone()
                .unwrap_or_else(|| format!("Exec: {}", ea.command)),
            passed,
            detail,
        });
    }

    // Screenshot assertions.
    for sa in &spec.assert.screenshot {
        let screenshot_path = paths.screenshots_dir.join("gate-check.ppm");
        let result = screenshot::capture(
            &paths.qmp_socket,
            &screenshot_path,
        )
        .await;

        let (passed, detail) = match result {
            Ok(info) => match sa.assert_type.as_str() {
                "non_blank" => {
                    let size_ok = sa
                        .min_size_bytes
                        .map(|min| info.size_bytes >= min)
                        .unwrap_or(true);
                    let blank_ok = !info.is_blank;
                    let ok = size_ok && blank_ok;
                    let d = if ok {
                        None
                    } else {
                        Some(format!(
                            "blank={}, size={} (min={})",
                            info.is_blank,
                            info.size_bytes,
                            sa.min_size_bytes.unwrap_or(0)
                        ))
                    };
                    (ok, d)
                }
                other => (false, Some(format!("Unknown screenshot type: {other}"))),
            },
            Err(e) => (false, Some(format!("Screenshot failed: {e}"))),
        };

        assertions.push(AssertionResult {
            description: sa
                .description
                .clone()
                .unwrap_or_else(|| format!("Screenshot: {}", sa.assert_type)),
            passed,
            detail,
        });
    }

    let passed = assertions.iter().filter(|a| a.passed).count();
    let failed = assertions.iter().filter(|a| !a.passed).count();
    let status = if failed == 0 { "passed" } else { "failed" };

    if failed == 0 {
        output::log_ok(&format!("Gate PASSED: {} ({passed} assertions)", spec.gate.name));
    } else {
        output::log_error(&format!(
            "Gate FAILED: {} ({failed}/{} assertions failed)",
            spec.gate.name,
            assertions.len()
        ));
    }

    // Copy screenshots to persistent output directory before VM is destroyed.
    let mut saved_screenshots: Vec<String> = Vec::new();
    if let Some(out_dir) = screenshots_out {
        if let Ok(entries) = std::fs::read_dir(&paths.screenshots_dir) {
            let _ = std::fs::create_dir_all(out_dir);
            for entry in entries.flatten() {
                let src = entry.path();
                if src.extension().and_then(|e| e.to_str()) == Some("ppm") {
                    let fname = format!(
                        "{}-{}.ppm",
                        spec.gate.sub_phase,
                        src.file_name()
                            .and_then(|n| n.to_str())
                            .unwrap_or("screenshot")
                    );
                    let dst = out_dir.join(&fname);
                    if std::fs::copy(&src, &dst).is_ok() {
                        saved_screenshots.push(dst.display().to_string());
                        output::log_info(&format!("Screenshot saved: {}", dst.display()));
                    }
                }
            }
        }
    }

    Ok(GateResult {
        gate: spec.gate.name.clone(),
        phase: spec.gate.phase.clone(),
        sub_phase: spec.gate.sub_phase.clone(),
        status: status.into(),
        assertions,
        passed,
        failed,
        elapsed_ms: start.elapsed().as_millis() as u64,
        error: None,
        screenshots: saved_screenshots,
    })
}

/// Run all gates matching a phase filter.
pub async fn run_gates(
    gate_dir: &Path,
    phase_filter: Option<&str>,
    image: &Path,
    screenshots_out: Option<&Path>,
) -> Vec<GateResult> {
    let mut specs = Vec::new();

    // Collect all .toml files recursively.
    if let Ok(entries) = walk_toml_files(gate_dir) {
        for path in entries {
            match crate::gate::definition::parse_gate_file(&path) {
                Ok(spec) => {
                    if let Some(phase) = phase_filter {
                        if spec.gate.phase == phase {
                            specs.push((path, spec));
                        }
                    } else {
                        specs.push((path, spec));
                    }
                }
                Err(e) => {
                    output::log_error(&format!(
                        "Failed to parse {}: {e}",
                        path.display()
                    ));
                }
            }
        }
    }

    // Sort by phase + sub_phase.
    specs.sort_by(|a, b| {
        let ka = format!("{}.{}", a.1.gate.phase, a.1.gate.sub_phase);
        let kb = format!("{}.{}", b.1.gate.phase, b.1.gate.sub_phase);
        ka.cmp(&kb)
    });

    output::log_info(&format!("Found {} gates to run", specs.len()));

    let mut results = Vec::new();
    for (path, spec) in &specs {
        output::log_info(&format!("Running gate: {} ({})", spec.gate.name, path.display()));
        let result = run_gate(spec, image, screenshots_out).await;
        results.push(result);
    }

    results
}

fn walk_toml_files(dir: &Path) -> std::io::Result<Vec<PathBuf>> {
    let mut files = Vec::new();
    if !dir.is_dir() {
        return Ok(files);
    }
    for entry in std::fs::read_dir(dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.is_dir() {
            files.extend(walk_toml_files(&path)?);
        } else if path.extension().and_then(|e| e.to_str()) == Some("toml") {
            files.push(path);
        }
    }
    Ok(files)
}
