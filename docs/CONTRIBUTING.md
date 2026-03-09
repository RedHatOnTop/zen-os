# Contributing to Zen OS

Thank you for your interest in contributing to Zen OS. This document covers the conventions and requirements for all contributions.

## Branch Naming

All branches must follow this naming convention:

| Prefix | Purpose | Example |
|--------|---------|---------|
| `feature/` | New functionality or enhancements | `feature/shelf-autohide` |
| `fix/` | Bug fixes | `fix/compositor-crash-on-resize` |
| `docs/` | Documentation-only changes | `docs/adr-001-in-process-shell` |

Branch names use lowercase kebab-case after the prefix. Keep them short and descriptive.

## Commit Message Format

All commit messages must be written in English and follow this format:

```
<scope>: <imperative summary>

<optional body>
```

Rules:
- Subject line uses imperative mood ("add", "fix", "remove", not "added", "fixes", "removed")
- Subject line is 72 characters or fewer
- Scope identifies the affected component (e.g., `compositor`, `shell`, `privacy-guard`, `docs`, `ci`)
- Body is optional but recommended for non-trivial changes
- Reference related issues or requirements where applicable

Examples:
```
compositor: add xdg_toplevel resize handler

Implements request_resize for xdg_shell surfaces. The compositor
now tracks grab state and applies size hints from the client.

Requirement: 2.1
```

```
docs: add ADR-001 for in-process shell decision
```

## Pull Request Process

1. Create a branch from `main` using the naming convention above
2. Make your changes, ensuring all quality gates pass locally
3. Run the full build and test suite before pushing:
   ```bash
   meson setup builddir --wipe
   meson compile -C builddir
   meson test -C builddir
   ```
4. Open a PR against `main` with a clear description of the change
5. All CI checks (lint, build, test) must pass before merge
6. At least one review approval is required

## Code Style

Zen OS is written in C17. All code must follow these conventions:

| Rule | Value |
|------|-------|
| Standard | C17 (`-std=c17`) |
| Brace style | K&R (opening brace on same line) |
| Indentation | 4 spaces (no tabs) |
| Line length | 100 columns maximum |
| Function/variable naming | `snake_case` |
| Constants/macros | `UPPER_SNAKE_CASE` |
| Types/structs | `PascalCase` |
| Include guards | `#ifndef ZEN_<MODULE>_<FILE>_H` |

### Resource Management

Every function that allocates resources must use the `goto cleanup` pattern to guarantee cleanup on all error paths:

```c
int my_function(void) {
    int ret = -1;
    char *buf = NULL;
    int fd = -1;

    buf = calloc(1, 4096);
    if (!buf) goto cleanup;

    fd = open("/dev/something", O_RDONLY);
    if (fd < 0) goto cleanup;

    /* ... actual work ... */
    ret = 0;

cleanup:
    if (fd >= 0) close(fd);
    free(buf);
    return ret;
}
```

### Banned Dependencies

GTK, Qt, Skia, and any heavyweight UI toolkit are permanently banned. All GUI rendering uses Cairo + Pango for widget content and SceneFX for compositor-level effects.

### D-Bus Errors

All D-Bus method implementations must return errors using the `ZenError` enum defined in `src/common/include/zen/dbus-errors.h`.

## Testing

All contributions must satisfy these testing requirements before merge:

- `meson compile -C builddir` exits 0 with 0 errors and 0 warnings
- `meson test -C builddir` exits 0 with all tests passing
- AddressSanitizer (ASan) reports 0 errors in debug builds
- UndefinedBehaviorSanitizer (UBSan) reports 0 errors in debug builds
- No memory leaks reported by LeakSanitizer

For QEMU-based integration testing, use `tools/zen-test-cli`. All test commands must be non-interactive.

### Writing Tests

- Unit tests go in `tests/unit/` and use the CMocka framework
- Integration tests go in `tests/integration/`
- Every new module should have corresponding unit tests
- Test file naming: `test_<module>.c` (e.g., `test_dbus_errors.c`)

## Language

All code, comments, commit messages, documentation, and file names must be written in English. No exceptions.
