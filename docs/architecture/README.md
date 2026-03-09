# Architecture Decision Records

This directory contains Architecture Decision Records (ADRs) for the Zen OS project. ADRs document significant technical decisions, their context, and rationale so that future contributors understand why the system is built the way it is.

## ADR Format

Each ADR follows this template:

```markdown
# ADR-NNN: Title

## Status
Accepted | Proposed | Deprecated | Superseded by ADR-XXX

## Context
What is the technical or business context that motivates this decision?

## Decision
What is the change that we are proposing or have agreed to implement?

## Consequences
What are the positive, negative, and neutral outcomes of this decision?
```

ADR files are named `NNN-short-title.md` (e.g., `001-in-process-shell.md`).

## ADR Index

| ADR | Title | Status |
|-----|-------|--------|
| ADR-001 | [In-process shell architecture](#adr-001-in-process-shell-architecture) | Planned |
| ADR-002 | [Cairo + Pango over GTK](#adr-002-cairo--pango-over-gtk) | Planned |
| ADR-003 | [OSTree layering over direct APT](#adr-003-ostree-layering-over-direct-apt) | Planned |
| ADR-004 | [SceneFX over raw wlroots scene API](#adr-004-scenefx-over-raw-wlroots-scene-api) | Planned |

## Planned ADR Summaries

### ADR-001: In-process shell architecture

The desktop shell (Shelf, App Launcher, Quick Settings, Notification Manager) is compiled into the compositor binary as an integrated C module rather than running as a separate Wayland client process. This eliminates IPC overhead, simplifies resource management, and allows the shell to directly manipulate the SceneFX scene graph for effects like blur and shadows. The tradeoff is tighter coupling between shell and compositor code, mitigated by a clean API boundary (`shell.h`).

### ADR-002: Cairo + Pango over GTK

All GUI rendering (shell widgets, OOBE wizard, installer) uses Cairo for 2D drawing and Pango for text layout instead of GTK or any other heavyweight toolkit. This eliminates a large transitive dependency tree, reduces memory footprint, and gives full control over rendering behavior on low-end hardware. The tradeoff is more manual work for layout and widget behavior, but the shell UI is simple enough that a toolkit is unnecessary overhead.

### ADR-003: OSTree layering over direct APT

Native Debian package installation uses OSTree layering to produce a new immutable deployment with the package integrated, rather than allowing direct APT modification of the root filesystem. This preserves the immutability guarantee of the OSTree root, enables atomic rollback of package installations, and prevents the system from entering an inconsistent state. The tradeoff is additional complexity in the package manager and slightly slower install times due to deployment creation.

### ADR-004: SceneFX over raw wlroots scene API

The compositor uses SceneFX as a drop-in replacement for the wlroots scene API to gain GPU-accelerated visual effects (Gaussian blur, drop shadows, per-corner rounding) without implementing custom shader pipelines. This provides a ChromeOS-inspired visual polish while keeping the compositor codebase focused on window management rather than graphics programming. The tradeoff is an additional subproject dependency and the requirement to use the GLES2 renderer (`WLR_RENDERER=gles2`) instead of the Pixman software renderer.
