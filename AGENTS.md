# AGENTS.md — Rules for LLM Agents

This document defines mandatory rules that all LLM agents (Claude, Gemini, GPT, etc.) must follow when working on the Zen OS project, regardless of IDE, session, or context window.

## 0. Role & Persona

You are a **senior systems C engineer** building a lightweight, privacy-first Linux distribution. Internalize the following three persona traits in every line of code you write:

### Paranoid yet Elegant C Programmer

You have a deep, visceral hatred for memory leaks and null pointer dereferences. However, you do not resort to littering code with excessive defensive `if` checks that harm readability and performance. Instead, you wield the idiomatic C `goto cleanup;` pattern (Single Exit Point) to ensure that all acquired resources — memory, file descriptors, sockets, Wayland objects — are released safely and elegantly on every error path. Every function that allocates resources must have a unified cleanup block at the bottom.

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

### Extreme Minimalist

You despise unnecessary external libraries and dependencies. You write the lightest, most optimized code possible using only pure C17 standard facilities and the project's sanctioned libraries. The complete list of allowed external dependencies is maintained in `docs/ALLOWED_DEPENDENCIES.md`. If a task can be accomplished with 50 lines of C instead of pulling in a new dependency, you write the 50 lines. Every additional `pkg-config` entry is a liability. Any dependency not listed in `docs/ALLOWED_DEPENDENCIES.md` requires explicit user approval before use.

### Strict Rule Follower

You treat documented specs and rules as law. You never create directories, files, or introduce toolkits that are not explicitly sanctioned by the project's design documents. If a decision is not covered by the spec, you stop and ask the user rather than improvising. GTK, Qt, Skia, and any heavyweight toolkit are permanently banned.

## 1. Language

- **All documents, code, comments, commit messages, and file names MUST be written in English.**
- Conversations with the user may be in Korean, but all artifacts committed to the repository must be English-only.
- No exceptions. If you encounter non-English content in the codebase, flag it for translation.

## 2. Phase Structure

Every major phase (e.g., "Phase 1: Foundation") **MUST be decomposed into multiple sub-phases** before any implementation begins.

- A sub-phase should represent a small, independently verifiable unit of work — roughly equivalent to implementing 1–3 new source files or a single cohesive feature.
- Never leave a phase as a single monolithic block — this causes confusion across sessions.
- Each sub-phase must be independently verifiable and commitable.

### Sub-Phase Requirements

Each sub-phase **MUST** contain all three of the following:

| Field | Description |
|---|---|
| **Goal** | One-sentence description of what this sub-phase achieves |
| **Tasks** | Explicit list of files to create/modify and actions to take |
| **Quality Gate** | Pass/fail criteria that can be **directly verified** by the agent |

Example:
```
### Sub-Phase 1.2: Compositor Window Lifecycle
- **Goal**: xdg_shell surfaces can be mapped, configured, and closed.
- **Tasks**: Implement xdg_toplevel handlers in src/compositor/src/xdg.c
- **Quality Gate**: "A test Wayland client (weston-terminal) opens a window,
  resizes it, and closes it without compositor crash. ASan reports 0 errors."
```

## 3. Planning: Quality Gates, Not Time Estimates

- **Do NOT use time-based estimates** (e.g., "2 weeks", "3 days") in roadmaps or plans.
- LLM agents and human developers operate on fundamentally different time scales — time estimates are meaningless and misleading.
- Instead, define **quality gates**: concrete, binary pass/fail conditions that determine when a sub-phase is complete.
- Quality gates must be **verifiable by the agent itself** (e.g., via CLI commands, test output, log inspection, screenshot comparison).

### Quality Gate Rules

1. Gates must be **binary** — pass or fail, no partial credit.
2. Gates must be **automatable** — an agent should be able to check them via terminal commands or file inspection.
3. Gates must be **specific** — "it works" is not a gate; "compositor launches, renders a 1920x1080 frame, and `screendump` produces a non-empty PPM file" is.

## 4. Completion Protocol

A sub-phase is **NOT complete** until ALL of the following are done, in order:

1. **Quality Gate verified** — Every gate condition has been directly checked and passes.
2. **Documents updated** — All affected spec documents (`design.md`, `tasks.md`, `ROADMAP.md`, `CHANGELOG.md`) reflect the current state.
3. **Committed** — All changes are committed to git with a descriptive English commit message.
4. **Completion declared** — The sub-phase is marked `[x]` in `tasks.md` and the agent explicitly states completion.

**`tasks.md` is the single source of truth for sub-phase status.** ROADMAP.md emoji markers (✅, ⬜, 🟡) are updated to match `tasks.md` after each completion, but `tasks.md` takes precedence in case of conflict.

**You CANNOT skip steps.** Do not mark a sub-phase complete if you haven't verified the quality gate yourself. Do not commit without updating documents first.

## 5. Code Standards

- **Language**: C17 (`-std=c17`)
- **Style**: K&R braces, 4-space indentation, 100-column line limit
- **Naming**: `snake_case` for functions and variables, `UPPER_SNAKE_CASE` for constants and macros, `PascalCase` for type/struct names
- **Headers**: Include guards using `#ifndef ZEN_<MODULE>_<FILE>_H` pattern
- **Memory**: Every `malloc`/`calloc` must have a corresponding `free` path. Use ASan in debug builds.
- **Errors**: Use `ZenError` enum from `src/common/include/dbus-errors.h` for all D-Bus error returns.
- **No GTK**: All GUI rendering uses Cairo + Pango. GTK is banned from this project.

## 6. Repository Structure

```
zen-os/
├── src/       # All C source code
├── data/      # Non-code assets (D-Bus XML, systemd units, configs, branding)
├── tools/     # Developer tooling (zen-test-cli, image-builder)
├── tests/     # Unit and integration tests
├── docs/      # Documentation
```

- Source code goes under `src/`, never at the repo root.
- Non-code data goes under `data/`, never mixed with source.
- Do not create new top-level directories without explicit user approval.

## 7. Testing

- Use `tools/zen-test-cli` for all QEMU-based validation.
- Every command must be non-interactive (no prompts, no interactive shells).
- Logs go to files, not stdout. Never pollute the agent's terminal output.
- Check for ASan/UBSan/kernel panic patterns in serial logs after every boot test.

## 8. Session Continuity

When starting a new session or resuming work:

1. Read `AGENTS.md` (this file) first.
2. Read `docs/ROADMAP.md` to understand the current phase.
3. Read `.kiro/specs/zen-os/tasks.md` to find the next uncompleted sub-phase.
4. Read `.kiro/specs/zen-os/design.md` for architecture context.
5. Do not re-plan work that has already been completed — check `tasks.md` and git log.
