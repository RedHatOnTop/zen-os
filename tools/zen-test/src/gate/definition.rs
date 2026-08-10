// zen-test — Quality gate TOML definition parser.

use serde::Deserialize;
use serde_json::Value as JsonValue;

/// Top-level gate specification parsed from a TOML file.
#[derive(Debug, Deserialize)]
pub struct GateSpec {
    pub gate: GateMeta,
    #[serde(default)]
    pub vm: Option<VmSpec>,
    #[serde(default)]
    pub pre_boot: Option<SetupSection>,
    #[serde(default)]
    pub setup: Option<SetupSection>,
    #[serde(default)]
    pub test: Option<TestSection>,
    #[serde(default)]
    pub assert: AssertSection,
}

/// Gate metadata.
#[derive(Debug, Deserialize)]
pub struct GateMeta {
    pub name: String,
    pub phase: String,
    pub sub_phase: String,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub tags: Option<Vec<String>>,
    #[serde(default = "default_gate_timeout")]
    pub timeout_seconds: u64,
}

fn default_gate_timeout() -> u64 {
    180
}

/// VM configuration override for this gate.
#[derive(Debug, Deserialize)]
pub struct VmSpec {
    #[serde(default = "default_ram")]
    pub ram_mb: u32,
    #[serde(default = "default_cpus")]
    pub cpus: u32,
}

fn default_ram() -> u32 {
    2048
}
fn default_cpus() -> u32 {
    2
}

/// Setup section: commands to run before assertions.
#[derive(Debug, Deserialize)]
pub struct SetupSection {
    #[serde(default)]
    pub exec: Vec<ExecStep>,
}

/// Test section: test actions to perform.
#[derive(Debug, Deserialize)]
pub struct TestSection {
    #[serde(default)]
    pub exec: Vec<ExecStep>,
    #[serde(default)]
    pub exec_loop: Vec<ExecLoopStep>,
    #[serde(default, rename = "step")]
    pub steps: Vec<TestStep>,
}

/// A single command to execute in the guest.
#[derive(Debug, Deserialize)]
pub struct ExecStep {
    pub command: String,
    #[serde(default = "default_exec_timeout")]
    pub timeout_seconds: u64,
}

fn default_exec_timeout() -> u64 {
    10
}

/// A command to execute multiple times in a loop.
#[derive(Debug, Deserialize)]
pub struct ExecLoopStep {
    pub command: String,
    pub count: u32,
    #[serde(default = "default_exec_timeout")]
    pub timeout_seconds: u64,
}

/// Ordered test action for interactive flows.
#[derive(Debug, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum TestStep {
    Exec {
        command: String,
        #[serde(default = "default_exec_timeout")]
        timeout_seconds: u64,
    },
    ExecLoop {
        command: String,
        count: u32,
        #[serde(default = "default_exec_timeout")]
        timeout_seconds: u64,
    },
    Wait {
        seconds: u64,
    },
    SendKey {
        keys: Vec<String>,
        #[serde(default = "default_send_key_wait_ms")]
        wait_ms: u64,
    },
    PointerMove {
        x: u32,
        y: u32,
        #[serde(default = "default_pointer_wait_ms")]
        wait_ms: u64,
    },
    PointerButton {
        button: String,
        #[serde(default)]
        action: PointerButtonAction,
        #[serde(default = "default_pointer_wait_ms")]
        wait_ms: u64,
    },
    PointerDrag {
        from_x: u32,
        from_y: u32,
        to_x: u32,
        to_y: u32,
        #[serde(default = "default_pointer_button")]
        button: String,
        #[serde(default = "default_drag_steps")]
        steps: u32,
        #[serde(default = "default_drag_step_wait_ms")]
        step_wait_ms: u64,
    },
    RestartService {
        service: String,
        #[serde(default = "default_exec_timeout")]
        timeout_seconds: u64,
    },
    Reboot {
        #[serde(default = "default_reboot_timeout")]
        timeout_seconds: u64,
    },
}

fn default_send_key_wait_ms() -> u64 {
    250
}

fn default_pointer_wait_ms() -> u64 {
    100
}

fn default_pointer_button() -> String {
    "left".to_string()
}

fn default_drag_steps() -> u32 {
    8
}

fn default_drag_step_wait_ms() -> u64 {
    25
}

fn default_reboot_timeout() -> u64 {
    120
}

#[derive(Debug, Default, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PointerButtonAction {
    Down,
    Up,
    #[default]
    Click,
    DoubleClick,
}

/// Assertion section: all assertions that must pass.
#[derive(Debug, Default, Deserialize)]
pub struct AssertSection {
    #[serde(default)]
    pub serial: Vec<SerialAssert>,
    #[serde(default)]
    pub serial_absent: Vec<SerialAbsentAssert>,
    #[serde(default)]
    pub exec: Vec<ExecAssert>,
    #[serde(default)]
    pub file_exists: Vec<FileExistsAssert>,
    #[serde(default)]
    pub file_json_value: Vec<FileJsonValueAssert>,
    #[serde(default)]
    pub exec_json_value: Vec<ExecJsonValueAssert>,
    #[serde(default)]
    pub exec_stdout_regex: Vec<ExecStdoutRegexAssert>,
    #[serde(default)]
    pub eventually_exec: Vec<EventuallyExecAssert>,
    #[serde(default)]
    pub eventually_absent: Vec<EventuallyAbsentAssert>,
    #[serde(default)]
    pub screenshot: Vec<ScreenshotAssert>,
}

/// Assert that a pattern IS present in the serial log.
#[derive(Debug, Deserialize)]
pub struct SerialAssert {
    pub pattern: String,
    #[serde(default)]
    pub description: Option<String>,
}

/// Assert that a pattern is NOT present in the serial log.
#[derive(Debug, Deserialize)]
pub struct SerialAbsentAssert {
    pub pattern: String,
    #[serde(default)]
    pub description: Option<String>,
}

/// Assert that a guest command exits with a specific code.
#[derive(Debug, Deserialize)]
pub struct ExecAssert {
    pub command: String,
    #[serde(default)]
    pub exit_code: i32,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default = "default_exec_timeout")]
    pub timeout_seconds: u64,
}

/// Assert that a file exists inside the guest.
#[derive(Debug, Deserialize)]
pub struct FileExistsAssert {
    pub path: String,
    #[serde(default)]
    pub description: Option<String>,
}

/// Assert that a JSON file contains an expected value at a dot-path query.
#[derive(Debug, Deserialize)]
pub struct FileJsonValueAssert {
    pub path: String,
    pub query: String,
    pub expected: JsonValue,
    #[serde(default)]
    pub description: Option<String>,
}

/// Assert that guest command stdout is JSON with an expected value at a dot-path query.
#[derive(Debug, Deserialize)]
pub struct ExecJsonValueAssert {
    pub command: String,
    pub query: String,
    pub expected: JsonValue,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default = "default_exec_timeout")]
    pub timeout_seconds: u64,
}

/// Assert that a guest command exits with a specific code and stdout matches a regex.
#[derive(Debug, Deserialize)]
pub struct ExecStdoutRegexAssert {
    pub command: String,
    pub pattern: String,
    #[serde(default)]
    pub exit_code: i32,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default = "default_exec_timeout")]
    pub timeout_seconds: u64,
}

/// Assert that a guest command eventually reaches the expected exit code.
#[derive(Debug, Deserialize)]
pub struct EventuallyExecAssert {
    pub command: String,
    #[serde(default)]
    pub exit_code: i32,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default = "default_eventually_timeout")]
    pub timeout_seconds: u64,
    #[serde(default = "default_poll_interval")]
    pub poll_interval_seconds: u64,
}

/// Assert that a guest command eventually reaches an "absent" state.
#[derive(Debug, Deserialize)]
pub struct EventuallyAbsentAssert {
    pub command: String,
    #[serde(default = "default_absent_exit_code")]
    pub exit_code: i32,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default = "default_eventually_timeout")]
    pub timeout_seconds: u64,
    #[serde(default = "default_poll_interval")]
    pub poll_interval_seconds: u64,
}

fn default_eventually_timeout() -> u64 {
    10
}

fn default_poll_interval() -> u64 {
    1
}

fn default_absent_exit_code() -> i32 {
    1
}

/// Assert screenshot properties.
#[derive(Debug, Deserialize)]
pub struct ScreenshotAssert {
    #[serde(rename = "type")]
    pub assert_type: String,
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    pub min_size_bytes: Option<u64>,
    #[serde(default)]
    pub anchor: Option<String>,
    #[serde(default)]
    pub x: Option<u32>,
    #[serde(default)]
    pub y: Option<u32>,
    #[serde(default)]
    pub width: Option<u32>,
    #[serde(default)]
    pub height: Option<u32>,
    #[serde(default)]
    pub min_non_black_pixels: Option<u64>,
    #[serde(default)]
    pub description: Option<String>,
}

/// Parse a gate TOML file.
pub fn parse_gate_file(path: &std::path::Path) -> crate::errors::Result<GateSpec> {
    let content = std::fs::read_to_string(path).map_err(|e| {
        crate::errors::ZenTestError::GateParseError(format!(
            "Failed to read {}: {e}",
            path.display()
        ))
    })?;
    let spec: GateSpec = toml::from_str(&content)?;
    Ok(spec)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_ordered_test_steps_and_new_assertions() {
        let toml = r#"
[gate]
name = "test"
phase = "2"
sub_phase = "2.99"

[[pre_boot.exec]]
command = "mkdir -p /root/.config/zenos"

[[test.step]]
kind = "exec"
command = "echo hi"

[[test.step]]
kind = "wait"
seconds = 2

[[test.step]]
kind = "send_key"
keys = ["meta_l", "ret"]

[[test.step]]
kind = "pointer_move"
x = 100
y = 200

[[test.step]]
kind = "pointer_button"
button = "left"
action = "click"

[[test.step]]
kind = "pointer_drag"
from_x = 100
from_y = 200
to_x = 300
to_y = 400

[[test.step]]
kind = "restart_service"
service = "zen-compositor.service"

[[test.step]]
kind = "reboot"

[[assert.exec_stdout_regex]]
command = "printf 'ready\n'"
pattern = "^ready$"

[[assert.file_exists]]
path = "/etc/zenos/theme.json"

[[assert.file_json_value]]
path = "/etc/zenos/theme.json"
query = "theme.mode"
expected = "dark"

[[assert.exec_json_value]]
command = "printf '{\"launcher\":{\"visible\":true}}\\n'"
query = "launcher.visible"
expected = true

[[assert.eventually_exec]]
command = "test -f /tmp/ready"
timeout_seconds = 5

[[assert.eventually_absent]]
command = "pgrep toast"
"#;

        let spec: GateSpec = toml::from_str(toml).unwrap();
        assert_eq!(spec.pre_boot.as_ref().unwrap().exec.len(), 1);
        assert_eq!(spec.test.as_ref().unwrap().steps.len(), 8);
        assert_eq!(spec.assert.file_exists.len(), 1);
        assert_eq!(spec.assert.file_json_value.len(), 1);
        assert_eq!(spec.assert.exec_json_value.len(), 1);
        assert_eq!(spec.assert.exec_stdout_regex.len(), 1);
        assert_eq!(spec.assert.eventually_exec.len(), 1);
        assert_eq!(spec.assert.eventually_absent.len(), 1);
    }

    #[test]
    fn absent_assert_defaults_to_exit_code_one() {
        let toml = r#"
[gate]
name = "test"
phase = "2"
sub_phase = "2.99"

[[assert.eventually_absent]]
command = "pgrep toast"
"#;

        let spec: GateSpec = toml::from_str(toml).unwrap();
        assert_eq!(spec.assert.eventually_absent[0].exit_code, 1);
    }
}
