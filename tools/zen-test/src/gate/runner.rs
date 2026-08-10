// zen-test — Quality gate execution engine.
//
// Orchestrates: boot → setup → test → assert → cleanup.
// Every step has its own timeout and failure handling.

use crate::agent;
use crate::errors::{Result, ZenTestError};
use crate::gate::definition::{
    ExecJsonValueAssert, ExecStdoutRegexAssert, FileJsonValueAssert, GateSpec,
    PointerButtonAction, TestSection, TestStep,
};
use crate::gate::offline;
use crate::output;
use crate::qmp::commands::{qmp_pointer_button, qmp_pointer_move_abs, qmp_send_key};
use crate::screenshot;
use crate::serial;
use crate::vm::config::{VmConfig, VmPaths, DEFAULT_DISPLAY};
use crate::vm::lifecycle;
use regex::Regex;
use serde::Serialize;
use serde_json::Value as JsonValue;
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
    #[serde(skip_serializing_if = "Option::is_none")]
    pub review_manifest: Option<String>,
}

/// Individual assertion result.
#[derive(Debug, Serialize)]
pub struct AssertionResult {
    pub description: String,
    pub passed: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub detail: Option<String>,
}

#[derive(Debug, Serialize)]
struct ReviewPackManifest<'a> {
    gate: &'a str,
    phase: &'a str,
    sub_phase: &'a str,
    status: &'a str,
    assertions: &'a [AssertionResult],
    screenshots: &'a [String],
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
            review_manifest: None,
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
            review_manifest: None,
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

    // 1b. Apply offline pre-boot mutations before the guest starts.
    if let Some(ref pre_boot) = spec.pre_boot {
        offline::run_pre_boot_exec(vm_name, &paths.disk, &pre_boot.exec)?;
    }

    // 2. Boot VM and wait for boot signal.
    lifecycle::vm_boot(vm_name, true, 120).await?;

    // 2b. If the gate has exec assertions (assert.exec, setup, or test),
    //     OR if any serial assertion checks for ZEN_BOOT_OK,
    //     ensure ZEN_BOOT_OK is in the serial log before proceeding.
    //     The boot-wait may have accepted the "login:" fallback before
    //     the compositor finished initializing.
    let has_exec = !spec.assert.exec.is_empty()
        || !spec.assert.file_exists.is_empty()
        || !spec.assert.file_json_value.is_empty()
        || !spec.assert.exec_json_value.is_empty()
        || !spec.assert.exec_stdout_regex.is_empty()
        || !spec.assert.eventually_exec.is_empty()
        || !spec.assert.eventually_absent.is_empty()
        || spec.setup.is_some()
        || spec.test.is_some();

    let boot_signal = boot_signal_from_env();

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
            return Err(ZenTestError::BootFailed(format!(
                "Required boot signal not found in serial log after 60s: {boot_signal}"
            )));
        }

        // Wait for guest agent to become responsive (up to 30s) — only needed for exec.
        if has_exec {
            output::log_info("Waiting for guest agent...");
            if wait_for_guest_agent_ready(&paths.agent_socket, 30).await {
                output::log_ok("Guest agent ready");
            } else {
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
        if !test.steps.is_empty() {
            run_ordered_test_steps(test, &paths).await?;
        } else {
            run_legacy_test_steps(test, &paths).await?;
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

    for fa in &spec.assert.file_exists {
        let command = format!("test -f {}", shell_quote(&fa.path));
        let result = agent::guest_exec(&paths.agent_socket, &command, 5).await;
        let (passed, detail) = match result {
            Ok(r) if r.exit_code == 0 => (true, None),
            Ok(r) => (
                false,
                Some(format!(
                    "Expected file to exist: {}. exit_code={}, stdout={:?}, stderr={:?}",
                    fa.path,
                    r.exit_code,
                    r.stdout.trim(),
                    r.stderr.trim()
                )),
            ),
            Err(e) => (false, Some(format!("File existence check failed: {e}"))),
        };

        assertions.push(AssertionResult {
            description: fa
                .description
                .clone()
                .unwrap_or_else(|| format!("File exists: {}", fa.path)),
            passed,
            detail,
        });
    }

    for fa in &spec.assert.file_json_value {
        assertions.push(run_file_json_value_assert(fa, &paths).await);
    }

    for ea in &spec.assert.exec_json_value {
        assertions.push(run_exec_json_value_assert(ea, &paths).await);
    }

    for ea in &spec.assert.exec_stdout_regex {
        assertions.push(run_exec_stdout_regex_assert(ea, &paths).await?);
    }

    for ea in &spec.assert.eventually_exec {
        assertions.push(run_eventually_exec_assert(
            ea.command.as_str(),
            ea.exit_code,
            ea.description
                .clone()
                .unwrap_or_else(|| format!("Eventually exec: {}", ea.command)),
            ea.timeout_seconds,
            ea.poll_interval_seconds,
            &paths,
        )
        .await);
    }

    for ea in &spec.assert.eventually_absent {
        assertions.push(run_eventually_exec_assert(
            ea.command.as_str(),
            ea.exit_code,
            ea.description
                .clone()
                .unwrap_or_else(|| format!("Eventually absent: {}", ea.command)),
            ea.timeout_seconds,
            ea.poll_interval_seconds,
            &paths,
        )
        .await);
    }

    // Screenshot assertions.
    for (idx, sa) in spec.assert.screenshot.iter().enumerate() {
        let screenshot_stem = screenshot_name(idx, sa.name.as_deref());
        let screenshot_path = paths
            .screenshots_dir
            .join(format!("{screenshot_stem}.ppm"));
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
                "region_non_blank" => {
                    let mut x = sa.x.unwrap_or(0);
                    let mut y = sa.y.unwrap_or(0);
                    let width = sa.width.unwrap_or(info.width.saturating_sub(x));
                    let height = sa.height.unwrap_or(info.height.saturating_sub(y));

                    if let Some(anchor) = sa.anchor.as_deref() {
                        match anchor {
                            "top_left" => {}
                            "top_right" => {
                                x = info.width.saturating_sub(width).saturating_sub(x);
                            }
                            "bottom_left" => {
                                y = info.height.saturating_sub(height).saturating_sub(y);
                            }
                            "bottom_right" => {
                                x = info.width.saturating_sub(width).saturating_sub(x);
                                y = info.height.saturating_sub(height).saturating_sub(y);
                            }
                            other => {
                                return Err(ZenTestError::GateParseError(format!(
                                    "Unknown screenshot anchor: {other}"
                                )));
                            }
                        }
                    }

                    match screenshot::analyze_region(
                        &screenshot_path,
                        x,
                        y,
                        width,
                        height,
                    ) {
                        Ok(region) => {
                            let min_non_black =
                                sa.min_non_black_pixels.unwrap_or(1);
                            let size_ok = sa
                                .min_size_bytes
                                .map(|min| info.size_bytes >= min)
                                .unwrap_or(true);
                            let ok = size_ok
                                && !region.is_blank
                                && region.non_black_pixels >= min_non_black;
                            let d = if ok {
                                None
                            } else {
                                Some(format!(
                                    "region=({},{} {}x{}), blank={}, non_black_pixels={} (min={}), screenshot_size={} (min={})",
                                    region.x,
                                    region.y,
                                    region.width,
                                    region.height,
                                    region.is_blank,
                                    region.non_black_pixels,
                                    min_non_black,
                                    info.size_bytes,
                                    sa.min_size_bytes.unwrap_or(0)
                                ))
                            };
                            (ok, d)
                        }
                        Err(e) => (false, Some(format!("Region analysis failed: {e}"))),
                    }
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
    let mut review_manifest = None;
    if let Some(out_dir) = screenshots_out {
        if let Ok(entries) = std::fs::read_dir(&paths.screenshots_dir) {
            let _ = std::fs::create_dir_all(out_dir);
            for entry in entries.flatten() {
                let src = entry.path();
                let ext = src.extension().and_then(|e| e.to_str());
                if matches!(ext, Some("ppm") | Some("png")) {
                    let fname = format!(
                        "{}-{}",
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

        let manifest_path = out_dir.join(format!("{}-review-pack.json", spec.gate.sub_phase));
        let manifest = ReviewPackManifest {
            gate: &spec.gate.name,
            phase: &spec.gate.phase,
            sub_phase: &spec.gate.sub_phase,
            status,
            assertions: &assertions,
            screenshots: &saved_screenshots,
        };

        match serde_json::to_string_pretty(&manifest) {
            Ok(content) => {
                if std::fs::write(&manifest_path, content).is_ok() {
                    review_manifest = Some(manifest_path.display().to_string());
                    output::log_info(&format!(
                        "Review manifest saved: {}",
                        manifest_path.display()
                    ));
                } else {
                    output::log_warn(&format!(
                        "Failed to write review manifest: {}",
                        manifest_path.display()
                    ));
                }
            }
            Err(e) => {
                output::log_warn(&format!("Failed to serialize review manifest: {e}"));
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
        review_manifest,
    })
}

async fn run_legacy_test_steps(test: &TestSection, paths: &VmPaths) -> Result<()> {
    for step in &test.exec {
        output::log_info(&format!("Test exec: {}", step.command));
        agent::guest_exec(&paths.agent_socket, &step.command, step.timeout_seconds).await?;
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

    Ok(())
}

async fn run_ordered_test_steps(test: &TestSection, paths: &VmPaths) -> Result<()> {
    let display = read_display_geometry(paths)?;

    for step in &test.steps {
        match step {
            TestStep::Exec {
                command,
                timeout_seconds,
            } => {
                output::log_info(&format!("Test exec: {command}"));
                agent::guest_exec(&paths.agent_socket, command, *timeout_seconds).await?;
            }
            TestStep::ExecLoop {
                command,
                count,
                timeout_seconds,
            } => {
                for i in 1..=*count {
                    output::log_info(&format!(
                        "Test loop [{}/{}]: {}",
                        i, count, command
                    ));
                    let _ = agent::guest_exec(&paths.agent_socket, command, *timeout_seconds).await;
                }
            }
            TestStep::Wait { seconds } => {
                output::log_info(&format!("Test wait: {}s", seconds));
                tokio::time::sleep(Duration::from_secs(*seconds)).await;
            }
            TestStep::SendKey { keys, wait_ms } => {
                output::log_info(&format!("Test send-key: {}", keys.join("+")));
                let key_refs: Vec<&str> = keys.iter().map(|s| s.as_str()).collect();
                qmp_send_key(&paths.qmp_socket, &key_refs).await?;
                if *wait_ms > 0 {
                    tokio::time::sleep(Duration::from_millis(*wait_ms)).await;
                }
            }
            TestStep::PointerMove { x, y, wait_ms } => {
                output::log_info(&format!("Pointer move: ({x}, {y})"));
                move_pointer(paths, &display, *x, *y).await?;
                if *wait_ms > 0 {
                    tokio::time::sleep(Duration::from_millis(*wait_ms)).await;
                }
            }
            TestStep::PointerButton {
                button,
                action,
                wait_ms,
            } => {
                output::log_info(&format!("Pointer button: {button} ({action:?})"));
                run_pointer_button_action(paths, button, action).await?;
                if *wait_ms > 0 {
                    tokio::time::sleep(Duration::from_millis(*wait_ms)).await;
                }
            }
            TestStep::PointerDrag {
                from_x,
                from_y,
                to_x,
                to_y,
                button,
                steps,
                step_wait_ms,
            } => {
                output::log_info(&format!(
                    "Pointer drag: ({from_x}, {from_y}) -> ({to_x}, {to_y})"
                ));
                run_pointer_drag(
                    paths,
                    &display,
                    *from_x,
                    *from_y,
                    *to_x,
                    *to_y,
                    button,
                    *steps,
                    *step_wait_ms,
                )
                .await?;
            }
            TestStep::RestartService {
                service,
                timeout_seconds,
            } => {
                output::log_info(&format!("Restarting service: {service}"));
                let cmd = format!(
                    "systemctl restart {} && systemctl is-active {} >/dev/null",
                    shell_quote(service),
                    shell_quote(service)
                );
                agent::guest_exec(&paths.agent_socket, &cmd, *timeout_seconds).await?;
            }
            TestStep::Reboot { timeout_seconds } => {
                output::log_info("Rebooting guest");
                reboot_guest(paths, *timeout_seconds).await?;
            }
        }
    }

    Ok(())
}

async fn run_exec_stdout_regex_assert(
    assert: &ExecStdoutRegexAssert,
    paths: &VmPaths,
) -> Result<AssertionResult> {
    let regex = Regex::new(&assert.pattern).map_err(|e| {
        ZenTestError::GateParseError(format!(
            "Invalid regex for exec_stdout_regex assertion '{}': {e}",
            assert.pattern
        ))
    })?;
    let result = agent::guest_exec(
        &paths.agent_socket,
        &assert.command,
        assert.timeout_seconds,
    )
    .await;

    let (passed, detail) = match result {
        Ok(r) => {
            let exit_ok = r.exit_code == assert.exit_code;
            let regex_ok = regex.is_match(r.stdout.trim());
            let ok = exit_ok && regex_ok;
            let detail = if ok {
                None
            } else {
                Some(format!(
                    "Expected exit code {}, got {}. stdout={:?}, stderr={:?}, pattern={}",
                    assert.exit_code,
                    r.exit_code,
                    r.stdout.trim(),
                    r.stderr.trim(),
                    assert.pattern
                ))
            };
            (ok, detail)
        }
        Err(e) => (false, Some(format!("Exec failed: {e}"))),
    };

    Ok(AssertionResult {
        description: assert
            .description
            .clone()
            .unwrap_or_else(|| format!("Exec stdout regex: {}", assert.command)),
        passed,
        detail,
    })
}

async fn run_file_json_value_assert(
    assert: &FileJsonValueAssert,
    paths: &VmPaths,
) -> AssertionResult {
    let command = format!("cat {}", shell_quote(&assert.path));
    let result = agent::guest_exec(&paths.agent_socket, &command, 5).await;

    let (passed, detail) = match result {
        Ok(r) if r.exit_code != 0 => (
            false,
            Some(format!(
                "Failed to read JSON file {}. exit_code={}, stdout={:?}, stderr={:?}",
                assert.path,
                r.exit_code,
                r.stdout.trim(),
                r.stderr.trim()
            )),
        ),
        Ok(r) => match serde_json::from_str::<JsonValue>(&r.stdout) {
            Ok(json) => evaluate_json_query(
                &json,
                &assert.query,
                &assert.expected,
                &format!("JSON file {}", assert.path),
            ),
            Err(e) => (
                false,
                Some(format!(
                    "Invalid JSON in {}: {}. stdout={:?}",
                    assert.path,
                    e,
                    r.stdout.trim()
                )),
            ),
        },
        Err(e) => (false, Some(format!("JSON file assertion failed: {e}"))),
    };

    AssertionResult {
        description: assert
            .description
            .clone()
            .unwrap_or_else(|| format!("JSON value: {} -> {}", assert.path, assert.query)),
        passed,
        detail,
    }
}

async fn run_exec_json_value_assert(
    assert: &ExecJsonValueAssert,
    paths: &VmPaths,
) -> AssertionResult {
    let result = agent::guest_exec(
        &paths.agent_socket,
        &assert.command,
        assert.timeout_seconds,
    )
    .await;

    let (passed, detail) = match result {
        Ok(r) if r.exit_code != 0 => (
            false,
            Some(format!(
                "Command returned exit code {}. stdout={:?}, stderr={:?}",
                r.exit_code,
                r.stdout.trim(),
                r.stderr.trim()
            )),
        ),
        Ok(r) => match serde_json::from_str::<JsonValue>(&r.stdout) {
            Ok(json) => evaluate_json_query(
                &json,
                &assert.query,
                &assert.expected,
                &format!("stdout of {}", assert.command),
            ),
            Err(e) => (
                false,
                Some(format!(
                    "Invalid JSON from command {}: {}. stdout={:?}",
                    assert.command,
                    e,
                    r.stdout.trim()
                )),
            ),
        },
        Err(e) => (false, Some(format!("Exec JSON assertion failed: {e}"))),
    };

    AssertionResult {
        description: assert
            .description
            .clone()
            .unwrap_or_else(|| format!("Exec JSON value: {} -> {}", assert.command, assert.query)),
        passed,
        detail,
    }
}

async fn run_eventually_exec_assert(
    command: &str,
    exit_code: i32,
    description: String,
    timeout_seconds: u64,
    poll_interval_seconds: u64,
    paths: &VmPaths,
) -> AssertionResult {
    let deadline = Instant::now() + Duration::from_secs(timeout_seconds);
    let poll_interval = Duration::from_secs(poll_interval_seconds.max(1));
    let mut last_detail: Option<String> = None;

    while Instant::now() < deadline {
        match agent::guest_exec(&paths.agent_socket, command, poll_interval_seconds.max(1)).await {
            Ok(r) if r.exit_code == exit_code => {
                return AssertionResult {
                    description,
                    passed: true,
                    detail: None,
                };
            }
            Ok(r) => {
                last_detail = Some(format!(
                    "Expected exit code {}, got {}. stdout={:?}, stderr={:?}",
                    exit_code,
                    r.exit_code,
                    r.stdout.trim(),
                    r.stderr.trim()
                ));
            }
            Err(e) => {
                last_detail = Some(format!("Exec failed: {e}"));
            }
        }

        tokio::time::sleep(poll_interval).await;
    }

    AssertionResult {
        description,
        passed: false,
        detail: Some(format!(
            "Condition not met within {}s. Last result: {}",
            timeout_seconds,
            last_detail.unwrap_or_else(|| "no attempts recorded".to_string())
        )),
    }
}

fn screenshot_name(index: usize, explicit: Option<&str>) -> String {
    let base = explicit.unwrap_or("gate-check");
    let sanitized: String = base
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '-'
            }
        })
        .collect();
    format!("{:02}-{}", index + 1, sanitized)
}

fn shell_quote(value: &str) -> String {
    let escaped = value.replace('\'', "'\"'\"'");
    format!("'{escaped}'")
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct DisplayGeometry {
    width: u32,
    height: u32,
}

fn lookup_json_value<'a>(value: &'a JsonValue, query: &str) -> Option<&'a JsonValue> {
    let mut current = value;
    for segment in query.split('.') {
        if segment.is_empty() {
            return None;
        }

        match current {
            JsonValue::Object(map) => {
                current = map.get(segment)?;
            }
            JsonValue::Array(items) => {
                let index = segment.parse::<usize>().ok()?;
                current = items.get(index)?;
            }
            _ => return None,
        }
    }

    Some(current)
}

fn evaluate_json_query(
    json: &JsonValue,
    query: &str,
    expected: &JsonValue,
    source: &str,
) -> (bool, Option<String>) {
    match lookup_json_value(json, query) {
        Some(value) if value == expected => (true, None),
        Some(value) => (
            false,
            Some(format!(
                "JSON query {} in {} expected {}, got {}",
                query,
                source,
                expected,
                value
            )),
        ),
        None => (
            false,
            Some(format!("JSON query {} not found in {}", query, source)),
        ),
    }
}

fn boot_signal_from_env() -> String {
    std::env::var("ZEN_BOOT_SIGNAL").unwrap_or_else(|_| "ZEN_BOOT_OK".to_string())
}

fn read_display_geometry(paths: &VmPaths) -> Result<DisplayGeometry> {
    let config_str = std::fs::read_to_string(&paths.config_file)?;
    let config: VmConfig = serde_json::from_str(&config_str)?;
    Ok(parse_display_geometry(&config.display))
}

fn parse_display_geometry(display: &str) -> DisplayGeometry {
    let fallback = parse_display_pair(DEFAULT_DISPLAY).unwrap_or((1920, 1080));
    let (width, height) = parse_display_pair(display).unwrap_or(fallback);
    DisplayGeometry { width, height }
}

fn parse_display_pair(display: &str) -> Option<(u32, u32)> {
    let (width, height) = display.split_once('x')?;
    let width = width.parse().ok()?;
    let height = height.parse().ok()?;
    Some((width, height))
}

async fn move_pointer(
    paths: &VmPaths,
    display: &DisplayGeometry,
    x: u32,
    y: u32,
) -> Result<()> {
    let abs_x = scale_pointer_coordinate(x, display.width);
    let abs_y = scale_pointer_coordinate(y, display.height);
    qmp_pointer_move_abs(&paths.qmp_socket, abs_x, abs_y).await
}

async fn run_pointer_button_action(
    paths: &VmPaths,
    button: &str,
    action: &PointerButtonAction,
) -> Result<()> {
    let normalized = normalize_pointer_button(button)?;

    match action {
        PointerButtonAction::Down => {
            qmp_pointer_button(&paths.qmp_socket, normalized, true).await?;
        }
        PointerButtonAction::Up => {
            qmp_pointer_button(&paths.qmp_socket, normalized, false).await?;
        }
        PointerButtonAction::Click => {
            qmp_pointer_button(&paths.qmp_socket, normalized, true).await?;
            tokio::time::sleep(Duration::from_millis(25)).await;
            qmp_pointer_button(&paths.qmp_socket, normalized, false).await?;
        }
        PointerButtonAction::DoubleClick => {
            for _ in 0..2 {
                qmp_pointer_button(&paths.qmp_socket, normalized, true).await?;
                tokio::time::sleep(Duration::from_millis(25)).await;
                qmp_pointer_button(&paths.qmp_socket, normalized, false).await?;
                tokio::time::sleep(Duration::from_millis(50)).await;
            }
        }
    }

    Ok(())
}

async fn run_pointer_drag(
    paths: &VmPaths,
    display: &DisplayGeometry,
    from_x: u32,
    from_y: u32,
    to_x: u32,
    to_y: u32,
    button: &str,
    steps: u32,
    step_wait_ms: u64,
) -> Result<()> {
    let normalized = normalize_pointer_button(button)?;
    let segments = steps.max(1);

    move_pointer(paths, display, from_x, from_y).await?;
    if step_wait_ms > 0 {
        tokio::time::sleep(Duration::from_millis(step_wait_ms)).await;
    }
    qmp_pointer_button(&paths.qmp_socket, normalized, true).await?;

    for step in 1..=segments {
        let x = interpolate_coordinate(from_x, to_x, step, segments);
        let y = interpolate_coordinate(from_y, to_y, step, segments);
        move_pointer(paths, display, x, y).await?;
        if step_wait_ms > 0 {
            tokio::time::sleep(Duration::from_millis(step_wait_ms)).await;
        }
    }

    qmp_pointer_button(&paths.qmp_socket, normalized, false).await?;
    Ok(())
}

fn interpolate_coordinate(start: u32, end: u32, step: u32, total_steps: u32) -> u32 {
    if total_steps == 0 {
        return end;
    }

    let start = start as i64;
    let end = end as i64;
    let delta = end - start;
    let value = start + (delta * step as i64) / total_steps as i64;
    value.clamp(0, u32::MAX as i64) as u32
}

fn scale_pointer_coordinate(coord: u32, size: u32) -> u32 {
    const QMP_ABS_MAX: u64 = 32_767;

    if size <= 1 {
        return 0;
    }

    let clamped = coord.min(size - 1) as u64;
    ((clamped * QMP_ABS_MAX) / (size as u64 - 1)) as u32
}

fn normalize_pointer_button(button: &str) -> Result<&str> {
    match button {
        "left" | "right" | "middle" | "wheel-up" | "wheel-down" => Ok(button),
        "wheel_up" => Ok("wheel-up"),
        "wheel_down" => Ok("wheel-down"),
        other => Err(ZenTestError::GateParseError(format!(
            "Unsupported pointer button: {other}"
        ))),
    }
}

async fn reboot_guest(paths: &VmPaths, timeout_seconds: u64) -> Result<()> {
    let boot_signal = boot_signal_from_env();
    let serial_offset = std::fs::read_to_string(&paths.serial_log)
        .map(|content| content.len())
        .unwrap_or(0);
    let reboot_command = "sh -c '(sleep 1; systemctl reboot) >/dev/null 2>&1 &'";

    match agent::guest_exec(&paths.agent_socket, reboot_command, 5).await {
        Ok(result) if result.exit_code == 0 => {}
        Ok(result) => {
            return Err(ZenTestError::GateFailed(format!(
                "Guest reboot command failed with exit code {}. stdout={:?}, stderr={:?}",
                result.exit_code,
                result.stdout.trim(),
                result.stderr.trim()
            )));
        }
        Err(ZenTestError::AgentError(_)) | Err(ZenTestError::AgentTimeout(_)) => {
            output::log_info("Guest agent disconnected during reboot request");
        }
        Err(e) => return Err(e),
    }

    match wait_for_serial_pattern_after_offset(
        &paths.serial_log,
        serial_offset,
        &[boot_signal.as_str()],
        Duration::from_secs(timeout_seconds),
        Duration::from_secs(1),
    )
    .await
    {
        serial::tail::WaitResult::PatternFound(_) => {
            output::log_ok("Reboot boot signal confirmed in serial log");
        }
        serial::tail::WaitResult::ErrorFound(pattern) => {
            return Err(ZenTestError::BootFailed(format!(
                "Fatal error during reboot: {pattern}"
            )));
        }
        serial::tail::WaitResult::Timeout => {
            return Err(ZenTestError::BootTimeout(timeout_seconds));
        }
    }

    output::log_info("Waiting for guest agent after reboot...");
    if wait_for_guest_agent_ready(&paths.agent_socket, 30).await {
        output::log_ok("Guest agent ready after reboot");
        return Ok(());
    }

    Err(ZenTestError::AgentError(
        "Guest agent not responsive after reboot".into(),
    ))
}

async fn wait_for_guest_agent_ready(agent_socket: &Path, timeout_seconds: u64) -> bool {
    let attempts = std::cmp::max(1, (timeout_seconds + 1) / 2);
    for _ in 0..attempts {
        match agent::guest_exec(agent_socket, "echo ready", 3).await {
            Ok(result) if result.exit_code == 0 => return true,
            _ => tokio::time::sleep(Duration::from_secs(2)).await,
        }
    }

    false
}

async fn wait_for_serial_pattern_after_offset(
    log_path: &Path,
    start_offset: usize,
    success_patterns: &[&str],
    deadline: Duration,
    poll_interval: Duration,
) -> serial::tail::WaitResult {
    let start = Instant::now();

    loop {
        if start.elapsed() >= deadline {
            return serial::tail::WaitResult::Timeout;
        }

        let content = match std::fs::read_to_string(log_path) {
            Ok(content) => content,
            Err(_) => {
                tokio::time::sleep(poll_interval).await;
                continue;
            }
        };

        let new_content = if start_offset < content.len() {
            &content[start_offset..]
        } else if start_offset > content.len() {
            content.as_str()
        } else {
            ""
        };

        if !new_content.is_empty() {
            for pattern in success_patterns {
                if new_content.contains(pattern) {
                    return serial::tail::WaitResult::PatternFound((*pattern).to_string());
                }
            }

            for error_pattern in crate::serial::patterns::ERROR_PATTERNS {
                if new_content.contains(error_pattern.pattern) {
                    return serial::tail::WaitResult::ErrorFound(
                        error_pattern.pattern.to_string(),
                    );
                }
            }
        }

        tokio::time::sleep(poll_interval).await;
    }
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_log_path() -> PathBuf {
        let unique = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        std::env::temp_dir().join(format!("zen-test-runner-{unique}.log"))
    }

    #[tokio::test]
    async fn waits_for_new_serial_pattern_after_offset() {
        let path = temp_log_path();
        std::fs::write(&path, "ZEN_BOOT_OK\n").unwrap();
        let offset = std::fs::read_to_string(&path).unwrap().len();
        let append_path = path.clone();

        tokio::spawn(async move {
            tokio::time::sleep(Duration::from_millis(50)).await;
            let mut file = std::fs::OpenOptions::new()
                .append(true)
                .open(&append_path)
                .unwrap();
            writeln!(file, "ZEN_BOOT_OK").unwrap();
        });

        let result = wait_for_serial_pattern_after_offset(
            &path,
            offset,
            &["ZEN_BOOT_OK"],
            Duration::from_secs(1),
            Duration::from_millis(10),
        )
        .await;

        assert!(matches!(result, serial::tail::WaitResult::PatternFound(_)));
        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn lookup_json_value_traverses_objects_and_arrays() {
        let json: JsonValue = serde_json::json!({
            "theme": {
                "mode": "dark",
                "palette": ["bg", "fg"]
            }
        });

        assert_eq!(
            lookup_json_value(&json, "theme.mode"),
            Some(&serde_json::json!("dark"))
        );
        assert_eq!(
            lookup_json_value(&json, "theme.palette.1"),
            Some(&serde_json::json!("fg"))
        );
        assert!(lookup_json_value(&json, "theme.palette.9").is_none());
    }

    #[test]
    fn parse_display_geometry_uses_fallback_for_invalid_values() {
        assert_eq!(
            parse_display_geometry("1280x720"),
            DisplayGeometry {
                width: 1280,
                height: 720
            }
        );
        assert_eq!(
            parse_display_geometry("invalid"),
            parse_display_geometry(DEFAULT_DISPLAY)
        );
    }

    #[test]
    fn pointer_coordinate_scaling_clamps_to_display_bounds() {
        assert_eq!(scale_pointer_coordinate(0, 1920), 0);
        assert_eq!(scale_pointer_coordinate(1919, 1920), 32_767);
        assert_eq!(scale_pointer_coordinate(5000, 1920), 32_767);
    }

    #[test]
    fn interpolate_coordinate_reaches_endpoints() {
        assert_eq!(interpolate_coordinate(10, 50, 0, 4), 10);
        assert_eq!(interpolate_coordinate(10, 50, 4, 4), 50);
        assert_eq!(interpolate_coordinate(50, 10, 2, 4), 30);
    }
}
