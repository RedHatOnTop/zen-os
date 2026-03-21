// zen-test — Quality gate TOML definition parser.

use serde::Deserialize;

/// Top-level gate specification parsed from a TOML file.
#[derive(Debug, Deserialize)]
pub struct GateSpec {
    pub gate: GateMeta,
    #[serde(default)]
    pub vm: Option<VmSpec>,
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

/// Assert screenshot properties.
#[derive(Debug, Deserialize)]
pub struct ScreenshotAssert {
    #[serde(rename = "type")]
    pub assert_type: String, // "non_blank"
    #[serde(default)]
    pub min_size_bytes: Option<u64>,
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
