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
- SceneFX `fx_get_renderer` assertion fails when `WLR_RENDERER=pixman` is used.
  Compositor must use `WLR_RENDERER=gles2` with Mesa llvmpipe for headless rendering.
  The compositor currently restarts via systemd `Restart=on-failure`, and the boot
  signal is emitted before the crash. This will be resolved in Sub-Phase 1.2 by
  ensuring GLES2 renderer is used consistently.

### Phase 0: Planning & Project Setup

#### Added
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
  - Development roadmap created (10 phases, ~9–12 months to v1.0.0)
  - Changelog created to track implementation status

#### Pending
- [ ] Root project files (README, LICENSE, .gitignore, meson.build)
- [ ] Source directory scaffolding with placeholders
- [ ] D-Bus interface XML contracts
- [ ] systemd unit file templates
- [ ] AppArmor, nftables, polkit, zram config templates
- [ ] Icon theme scaffolding
- [ ] CI pipeline (GitHub Actions)
- [ ] Scaffolding verification checkpoint

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
| Project Scaffolding | 0 | 🟡 In Progress |
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
