# Changelog — Zen OS

All notable changes to this project will be documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows the milestone scheme defined in [ROADMAP.md](./ROADMAP.md).

---

## [Unreleased]

### Phase 1: Foundation — Sub-Phase 1.1: Minimal Compositor — Empty Frame

#### Added
- **2026-03-04**: Sub-Phase 1.1 complete — minimal compositor verified end-to-end
  - `src/compositor/src/main.c`: wlroots + SceneFX compositor with headless backend,
    scene graph, output layout, frame handler, and `ZEN_BOOT_OK` serial signal
  - `src/compositor/include/zen/compositor.h`: `ZenCompositor` and `ZenOutput` structs
  - `src/compositor/meson.build`: SceneFX + wayland-server + xkbcommon dependencies
  - `src/common/include/zen/dbus-errors.h`: `ZenError` enum for future D-Bus errors
  - `src/common/src/dbus-errors.c`: Error-to-string mapping
  - `src/common/meson.build`: Static library build for zen-common
  - `tools/image-builder/build-test-image.sh`: Ubuntu 24.04 based qcow2 test image builder
  - `tools/zen-test-cli/`: VM lifecycle management (create, start, stop, screenshot, destroy)
  - `subprojects/scenefx.wrap`, `wlroots.wrap`, `wayland.wrap`, `pixman.wrap`: Meson wraps

#### Quality Gate Results (Sub-Phase 1.1)
- [x] `meson compile -C builddir` — 0 errors, 0 warnings
- [x] `build-test-image.sh` produces bootable qcow2 (1049 MB)
- [x] QEMU boot completes in ~20 seconds
- [x] Screenshot PPM file: 864 KB (> 1 KB threshold)
- [x] `ZEN_BOOT_OK` signal present in serial log
- [x] AddressSanitizer errors: 0
- [x] LeakSanitizer errors: 0

#### Known Issues
- ~~SceneFX `fx_get_renderer` assertion fails when `WLR_RENDERER=pixman` is used.~~
  **Resolved**: Defense-in-depth fix applied in `src/compositor/src/main.c` — environment variable
  enforcement (`WLR_RENDERER=gles2`) plus post-creation `wlr_renderer_is_fx()` runtime check.
  See `.kiro/specs/scenefx-renderer-fix/` for full bugfix spec.

### Phase 0: Planning & Project Setup

#### Added
- **2026-03-09**: Phase 0 complete — all planning and project setup scaffolding verified
  - Sub-Phase 0.8: D-Bus interface contracts — 5 XML files under `data/dbus/`, all valid XML, 25+ methods defined
  - Sub-Phase 0.9: systemd unit files — 8 units under `data/systemd/` with correct dependency ordering
  - Sub-Phase 0.10: Security configs — 6 AppArmor profiles, nftables firewall, polkit rules, zram config
  - Sub-Phase 0.11: Branding — icon theme with 49 directories, placeholder SVG logo
  - Sub-Phase 0.12: Browser configs — policies.json, user.js, userChrome.css for privacy hardening
  - Sub-Phase 0.13: Documentation — CONTRIBUTING.md and architecture ADR index
  - Sub-Phase 0.14: CI pipeline — GitHub Actions with lint, build (gcc+clang matrix), test jobs
  - Sub-Phase 0.15: Verification checkpoint — all quality gates passed
  - `meson setup builddir --wipe` exits 0
  - `meson compile -C builddir` exits 0 with 0 errors
  - `meson test -C builddir` exits 0 with 1 test passed (4 sub-tests)
  - `xmllint --noout data/dbus/*.xml` exits 0
  - 10 meson.build files in src/, 49 icon directories, 6 AppArmor profiles, 8 systemd units

- **2026-03-05**: Sub-Phase 0.7 complete — test directory scaffolding
  - `tests/unit/meson.build`: CMocka dependency, test-dbus-errors executable
  - `tests/unit/test_dbus_errors.c`: 4 test functions for `zen_error_string()` and `zen_error_dbus_name()`
  - `tests/integration/meson.build`: Placeholder for future D-Bus integration tests
  - `tests/integration/test_dbus.c`: Placeholder for inter-component D-Bus tests
  - Root `meson.build` updated with conditional `enable_tests` test inclusion
  - `meson_options.txt` updated with `enable_tests` option (default: true)
  - All 4 unit tests pass under ASan+UBSan

- **2026-03-05**: Sub-Phase 0.6 complete — source scaffolding for all remaining components
  - `src/shell/`: Shell, Shelf, App Launcher, Quick Settings, Notifications (static library with Cairo + Pango)
  - `src/daemons/resource-manager/`: PSI monitoring, cgroups, zram, Waydroid lifecycle (libsystemd)
  - `src/daemons/privacy-guard/`: Telemetry blocking, nftables, audit log (libsystemd)
  - `src/daemons/update-manager/`: OSTree atomic updates, rollback (libsystemd, libostree)
  - `src/daemons/package-manager/`: Flatpak + OSTree layering (libsystemd, libostree, libflatpak)
  - `src/session/`: PAM/logind session management (libsystemd)
  - `src/oobe/`: Out-of-box experience wizard (Cairo + Pango)
  - `src/installer/`: Disk partitioning, LUKS encryption, OSTree deployment (Cairo + Pango)
  - `src/common/include/zen/models.h`: 13 data model structs with init/cleanup functions
  - `src/common/include/zen/dbus-helpers.h`: sd-bus utility function stubs
  - All 16 headers use `#ifndef ZEN_<MODULE>_<FILE>_H` include guard pattern
  - 10 `meson.build` files across src/ — all compile cleanly

- **2026-03-02**: Project inception — requirements document created
  - 16 requirement areas defined: boot/init, compositor, Zen Browser, native apps, Waydroid, privacy, OSTree updates, package management, session management, OOBE, low-end hardware/kiosk, networking, security, accessibility, config persistence, desktop UI/UX
  - Hardware profiles defined: Full Desktop (4 GB+) and Kiosk/Low-End (2 GB)
  - OSTree/APT architectural conflict resolved via OSTree layering
  - Waydroid memory constraints addressed: 4 GB minimum, cgroups v2 freezer, zram, PSI monitoring
  - UI/UX requirements added: ChromeOS-inspired layout with Zen Browser visual identity

- **2026-03-02**: Design document created
  - Architecture defined: 5-layer system (OS Base → System Services → Application Runtime → Desktop Shell → User-Facing)
  - 12 components specified with D-Bus interfaces and data models
  - Technology stack selected: C17, Meson, wlroots, SceneFX, Cairo+Pango, sd-bus, libostree, libflatpak, PipeWire
  - Project structure defined
  - Boot sequence and component interaction diagrams created

- **2026-03-02**: Planning phase initiated
  - Design document refocused to planning & project setup
  - Implementation task list created for scaffolding phase
  - Development roadmap created (11 phases, 0–10, each gated by quality gates)
  - Changelog created to track implementation status

#### Completed
- [x] Root project files (meson.build, meson_options.txt, AGENTS.md, .gitignore, .clang-format)
- [x] Source directory scaffolding with placeholders (10 components)
- [x] D-Bus interface XML contracts (5 interfaces, 25+ methods)
- [x] systemd unit file templates (8 units)
- [x] AppArmor, nftables, polkit, zram config templates
- [x] Icon theme scaffolding (49 directories, placeholder SVG)
- [x] Browser privacy-hardening configs (policies.json, user.js, userChrome.css)
- [x] Documentation (CONTRIBUTING.md, architecture ADR index)
- [x] CI pipeline (GitHub Actions — lint, build, test)
- [x] Scaffolding verification checkpoint — all quality gates passed

---

## Status Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Not Started |
| 🟡 | In Progress |
| ✅ | Complete |
| ❌ | Blocked |
| ⏭️ | Skipped |

## Component Status Overview

| Component | Phase | Status |
|-----------|-------|--------|
| Project Scaffolding | 0 | ✅ Complete |
| Boot / OSTree | 1 | ⬜ Not Started |
| Compositor (wlroots) | 1 | 🟡 In Progress (Sub-Phase 1.1 done) |
| Session Manager | 1 | ⬜ Not Started |
| Shelf | 2 | ⬜ Not Started |
| App Launcher | 2 | ⬜ Not Started |
| Quick Settings | 2 | ⬜ Not Started |
| Notification Manager | 2 | ⬜ Not Started |
| Zen Browser | 3 | ⬜ Not Started |
| Package Manager | 3 | ⬜ Not Started |
| Privacy Guard | 4 | ⬜ Not Started |
| Update Manager | 4 | ⬜ Not Started |
| Resource Manager | 4 | ⬜ Not Started |
| Network Manager | 4 | ⬜ Not Started |
| Waydroid | 5 | ⬜ Not Started |
| Security (AppArmor/FW) | 6 | ⬜ Not Started |
| OOBE | 7 | ⬜ Not Started |
| Accessibility | 7 | ⬜ Not Started |
| Kiosk Mode | 7 | ⬜ Not Started |
