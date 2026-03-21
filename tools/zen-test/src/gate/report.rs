// zen-test — Gate result reporting (JSON, TAP).

use crate::gate::runner::GateResult;

/// Generate a TAP v13 report string.
pub fn to_tap(results: &[GateResult]) -> String {
    let mut lines = Vec::new();
    lines.push("TAP version 13".to_string());

    let total: usize = results.iter().map(|r| r.assertions.len()).sum();
    lines.push(format!("1..{total}"));

    let mut test_num = 0;
    for result in results {
        for assertion in &result.assertions {
            test_num += 1;
            let status = if assertion.passed { "ok" } else { "not ok" };
            lines.push(format!(
                "{status} {test_num} - [{}] {}",
                result.gate, assertion.description
            ));
            if let Some(ref detail) = assertion.detail {
                lines.push(format!("  ---"));
                lines.push(format!("  message: {detail}"));
                lines.push(format!("  ..."));
            }
        }
    }

    lines.join("\n")
}

/// Generate a summary line for the gate results.
pub fn summary(results: &[GateResult]) -> String {
    let total = results.len();
    let passed = results.iter().filter(|r| r.status == "passed").count();
    let failed = results.iter().filter(|r| r.status == "failed").count();
    let timeout = results.iter().filter(|r| r.status == "timeout").count();
    let error = results.iter().filter(|r| r.status == "error").count();

    format!("{total} gates: {passed} passed, {failed} failed, {timeout} timeout, {error} error")
}
