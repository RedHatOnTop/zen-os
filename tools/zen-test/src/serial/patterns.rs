// zen-test — Serial log error pattern registry.

use serde::Serialize;

/// A known error pattern with its category tag.
pub struct ErrorPattern {
    pub pattern: &'static str,
    pub tag: &'static str,
}

/// All error patterns to scan for in serial logs.
pub const ERROR_PATTERNS: &[ErrorPattern] = &[
    ErrorPattern {
        pattern: "ERROR: AddressSanitizer",
        tag: "asan",
    },
    ErrorPattern {
        pattern: "ERROR: LeakSanitizer",
        tag: "lsan",
    },
    ErrorPattern {
        pattern: "ERROR: UndefinedBehaviorSanitizer",
        tag: "ubsan",
    },
    ErrorPattern {
        pattern: "Kernel panic",
        tag: "kernel_panic",
    },
    ErrorPattern {
        pattern: "BUG:",
        tag: "kernel_bug",
    },
    ErrorPattern {
        pattern: "Oops:",
        tag: "kernel_oops",
    },
    ErrorPattern {
        pattern: "segfault",
        tag: "segfault",
    },
    ErrorPattern {
        pattern: "SIGABRT",
        tag: "sigabrt",
    },
    ErrorPattern {
        pattern: "Assertion",
        tag: "assertion",
    },
];

/// A single pattern match found in the serial log.
#[derive(Debug, Serialize, Clone)]
pub struct PatternMatch {
    pub pattern: String,
    pub tag: String,
    pub line_number: usize,
    pub line: String,
}
