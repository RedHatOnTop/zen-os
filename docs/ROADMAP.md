# Zen OS Development Roadmap

## Version Scheme

Zen OS follows a milestone-based versioning scheme during development:
- **0.1.x** — Foundation (boot, compositor, session)
- **0.2.x** — Desktop Shell (Shelf, App Launcher, Quick Settings)
- **0.3.x** — Application Ecosystem (Browser, Flatpak, OSTree layering)
- **0.4.x** — System Services (Privacy Guard, Resource Manager, Networking)
- **0.5.x** — Android Support (Waydroid integration)
- **0.6.x** — Security Hardening (AppArmor, firewall, encryption)
- **0.7.x** — Polish & Accessibility
- **0.8.x** — Integration Testing & Stabilization
- **0.9.x** — Release Candidates
- **1.0.0** — First Stable Release

---

## Reference Hardware

- **Primary (Development/Testing)**: QEMU/KVM x86_64 VM — 2 vCPUs, 2–4 GB RAM, 32 GB virtio disk, virtio-gpu, 1920x1080
- **Secondary (Physical Reference)**: Lenovo IdeaPad Slim 3i Chromebook 14 (2024) — Intel N100, 4 GB LPDDR5, 64 GB eMMC, Intel UHD, 14" FHD IPS

---

## Phase 0: Planning & Project Setup

**Goal**: Establish project structure, documentation, build system, interface contracts, configuration templates, and CI pipeline so that implementation phases can proceed cleanly.

**Status**: ✅ Complete (All Sub-Phases 0.1–0.15 verified)

> **Note**: Sub-Phase 1.1 (Minimal Compositor — Empty Frame) was completed ahead of Phase 0 as a proof-of-concept to validate the wlroots + SceneFX stack. Phase 0 is now complete (all sub-phases 0.1–0.15 verified).

### Sub-Phase 0.1: Root Project Files and Build System ✅

- **Goal**: Root meson.build, meson_options.txt, AGENTS.md, .gitignore are in place and `meson setup builddir` succeeds.
- **Tasks**:
  - Write `meson.build` declaring project, C17 standard, subprojects
  - Write `meson_options.txt` with enable_waydroid, enable_kiosk options
  - Write `AGENTS.md` with all agent rules
  - Write `.gitignore` for C/Meson projects
- **Quality Gate**:
  - [x] `meson setup builddir` exits 0
  - [x] `meson_options.txt` contains enable_waydroid and enable_kiosk options
  - [x] `AGENTS.md` exists and contains all 9 sections (0–8)
  - [x] `.gitignore` excludes `builddir/`, `*.o`, `*.so`, `.kiro/`

### Sub-Phase 0.2: Documentation Scaffolding ✅

- **Goal**: docs/ directory contains ROADMAP.md and CHANGELOG.md with initial content.
- **Tasks**:
  - Write `docs/ROADMAP.md` with phased development plan
  - Write `docs/CHANGELOG.md` with project inception entry
- **Quality Gate**:
  - [x] `docs/ROADMAP.md` exists and contains all 11 phases (0–10)
  - [x] `docs/CHANGELOG.md` exists and contains at least one dated entry

### Sub-Phase 0.3: Source Directory Scaffolding ✅

- **Goal**: src/common and src/compositor directories exist with compilable placeholder code.
- **Tasks**:
  - Create `src/common/` with meson.build, dbus-errors.h, dbus-errors.c
  - Create `src/compositor/` with meson.build, compositor.h, main.c
- **Quality Gate**:
  - [x] `src/common/meson.build` declares zen-common static library
  - [x] `src/common/include/zen/dbus-errors.h` defines `ZenError` enum with `ZEN_ERR_COUNT` sentinel
  - [x] `src/compositor/meson.build` declares zen-compositor executable with scenefx, wayland-server, xkbcommon dependencies
  - [x] `meson compile -C builddir` exits 0 with 0 errors

### Sub-Phase 0.4: Subproject Wraps ✅

- **Goal**: SceneFX, wlroots, wayland, and pixman are available as Meson subprojects.
- **Tasks**:
  - Write `subprojects/scenefx.wrap`
  - Write `subprojects/wlroots.wrap`
  - Write `subprojects/wayland.wrap`
  - Write `subprojects/pixman.wrap`
- **Quality Gate**:
  - [x] All 4 `.wrap` files exist in `subprojects/`
  - [x] `meson setup builddir` resolves all subproject dependencies without error
  - [x] `meson compile -C builddir` builds compositor against subproject SceneFX

### Sub-Phase 0.5: Testing Infrastructure ✅

- **Goal**: zen-test-cli and QEMU test image builder are functional for automated compositor testing.
- **Tasks**:
  - Create `tools/zen-test-cli/zen-test-cli` main entry point
  - Create `tools/zen-test-cli/lib/` helper scripts (config.sh, qmp.sh, boot.sh, vm.sh, scenarios.sh)
  - Create `tools/image-builder/build-test-image.sh`
  - Create `.github/workflows/qemu-test.yml`
- **Quality Gate**:
  - [x] `tools/zen-test-cli/zen-test-cli --help` exits 0
  - [x] `tools/image-builder/build-test-image.sh` produces a bootable qcow2 image
  - [x] `.github/workflows/qemu-test.yml` exists with valid YAML syntax

### Sub-Phase 0.6: Source Scaffolding — Shell, Daemons, Session, OOBE, Installer ✅

- **Goal**: All remaining src/ directories exist with placeholder C files and meson.build files that compile successfully.
- **Tasks**:
  - Create `src/shell/` with meson.build, include/ (shell.h, shelf.h, app-launcher.h, quick-settings.h, notifications.h), src/ (shell.c, shelf.c, app-launcher.c, quick-settings.c, notifications.c)
  - Create `src/daemons/resource-manager/` with meson.build, include/resource-manager.h, src/resource-manager.c
  - Create `src/daemons/privacy-guard/` with meson.build, include/privacy-guard.h, src/privacy-guard.c
  - Create `src/daemons/update-manager/` with meson.build, include/update-manager.h, src/update-manager.c
  - Create `src/daemons/package-manager/` with meson.build, include/package-manager.h, src/package-manager.c
  - Create `src/session/` with meson.build, include/session.h, src/session.c
  - Create `src/oobe/` with meson.build, include/oobe.h, src/oobe.c
  - Create `src/installer/` with meson.build, include/installer.h, src/installer.c
  - Create `src/common/include/zen/models.h` with all data model struct definitions from design.md
  - Create `src/common/include/zen/dbus-helpers.h` with sd-bus utility function declarations (stubs)
  - Create `src/common/src/models.c` and `src/common/src/dbus-helpers.c` with placeholder implementations
  - Update root `meson.build` to include all new subdir() entries
  - All placeholder `.c` files contain only a comment describing the component's purpose and an empty `main()` or stub functions
- **Quality Gate**:
  - [x] All directories listed above exist with the specified files
  - [x] Every `.h` file has a proper include guard using `#ifndef ZEN_<MODULE>_<FILE>_H` pattern
  - [x] `meson setup builddir --wipe` exits 0
  - [x] `meson compile -C builddir` exits 0 with 0 errors
  - [x] `grep -r 'ZEN_' src/*/include/ | wc -l` returns ≥ 15 (16 headers found)

### Sub-Phase 0.7: Test Directory Scaffolding ✅

- **Goal**: tests/ directory has unit and integration test scaffolding with CMocka dependency.
- **Tasks**:
  - Create `tests/unit/meson.build` with CMocka dependency declaration
  - Create `tests/unit/test_dbus_errors.c` — unit test for `zen_error_string()` and `zen_error_dbus_name()` (actual test, not placeholder)
  - Create `tests/integration/meson.build` placeholder
  - Create `tests/integration/test_dbus.c` placeholder for future D-Bus integration tests
  - Update root `meson.build` to include `subdir('tests/unit')` and `subdir('tests/integration')` conditionally via `enable_tests` option
  - Add `enable_tests` option to `meson_options.txt` if not present
- **Quality Gate**:
  - [x] `tests/unit/meson.build` exists and declares CMocka dependency
  - [x] `tests/unit/test_dbus_errors.c` exists and contains 4 test functions (≥ 2 required)
  - [x] `meson test -C builddir --suite unit` exits 0 with 1 test passed (4 sub-tests)
  - [x] `tests/integration/meson.build` exists

### Sub-Phase 0.8: D-Bus Interface Contracts ✅

- **Goal**: All 5 D-Bus XML interface definitions exist under data/dbus/ and are well-formed XML.
- **Tasks**:
  - Create `data/dbus/org.zenos.Compositor.xml` — methods: LaunchApp, PinToShelf, UnpinFromShelf, GetRunningApps, SetShelfConfig, ToggleDarkMode, ShowNotification
  - Create `data/dbus/org.zenos.UpdateManager.xml` — methods: CheckForUpdates, ApplyUpdate, Rollback, GetDeployments, GetSecurityUpdates; signals: UpdateAvailable, DeploymentChanged
  - Create `data/dbus/org.zenos.PackageManager.xml` — methods: InstallFlatpak, RemoveFlatpak, LayerPackage, UnlayerPackage, ListInstalled, Search; signals: PackageInstalled, PackageRemoved
  - Create `data/dbus/org.zenos.PrivacyGuard.xml` — methods: GetBlockedRequests, GetAuditLog, ReloadRules; signals: RequestBlocked
  - Create `data/dbus/org.zenos.ResourceManager.xml` — methods: GetMemoryPressure, FreezeContainer, ThawContainer, GetResourceBudgets, SetWaydroidEnabled; signals: MemoryPressureAlert, ContainerStateChanged
  - Each XML file must include `<node>`, `<interface>`, `<method>`, `<arg>` elements with `name`, `type`, and `direction` attributes
  - Each XML file must include `<annotation>` elements documenting the interface purpose
- **Quality Gate**:
  - [x] All 5 XML files exist under `data/dbus/`
  - [x] `xmllint --noout data/dbus/*.xml` exits 0 for all files (well-formed XML)
  - [x] Each XML file contains at least one `<interface>` element with `name` attribute starting with `org.zenos.`
  - [x] Each `<method>` element has at least one `<arg>` child with `type` and `direction` attributes
  - [x] `grep -c '<method' data/dbus/*.xml | awk -F: '{sum+=$2} END {print sum}'` returns ≥ 20 (total methods across all interfaces)

### Sub-Phase 0.9: systemd Unit File Templates ✅

- **Goal**: All systemd service and target unit files exist under data/systemd/ with correct dependency ordering.
- **Tasks**:
  - Create `data/systemd/zenos-compositor.service` — Type=simple, After=systemd-logind.service, Environment=WLR_RENDERER=gles2, ExecStart=/usr/bin/zen-compositor, Restart=on-failure
  - Create `data/systemd/zenos-resource-manager.service` — Type=dbus, BusName=org.zenos.ResourceManager, Before=zenos-compositor.service, After=local-fs.target
  - Create `data/systemd/zenos-privacy-guard.service` — Type=dbus, BusName=org.zenos.PrivacyGuard, After=network-pre.target, Before=network.target
  - Create `data/systemd/zenos-update-manager.service` — Type=dbus, BusName=org.zenos.UpdateManager, After=network-online.target
  - Create `data/systemd/zenos-package-manager.service` — Type=dbus, BusName=org.zenos.PackageManager, After=zenos-update-manager.service
  - Create `data/systemd/zenos-boot-check.service` — Type=oneshot, Before=sysinit.target, ExecStart=/usr/libexec/zenos-boot-check (OSTree health check)
  - Create `data/systemd/zenos-headless.target` — AllowIsolate=yes, Conflicts=zenos-compositor.service
  - Create `data/systemd/zenos-session@.service` — template unit for per-user session, After=systemd-logind.service
- **Quality Gate**:
  - [x] All 8 unit files exist under `data/systemd/`
  - [x] Each `.service` file contains `[Unit]` and `[Service]` sections; non-template services also contain `[Install]` sections
  - [x] `zenos-headless.target` contains `[Unit]` and `[Install]` sections with `AllowIsolate=yes`
  - [x] `grep -l 'After=' data/systemd/*.service | wc -l` returns ≥ 5 (dependency ordering present)
  - [x] `grep -l 'BusName=org.zenos' data/systemd/*.service | wc -l` returns ≥ 4 (D-Bus activation configured)
  - [x] No unit file references a non-existent target or service name

### Sub-Phase 0.10: Security and System Configuration Templates ✅

- **Goal**: AppArmor profiles, nftables firewall rules, polkit rules, and zram configuration templates exist under data/.
- **Tasks**:
  - Create `data/apparmor/zenos-compositor` — restrict to Wayland socket, DRI devices, input devices, /usr/share/fonts, /usr/share/icons
  - Create `data/apparmor/zenos-privacy-guard` — restrict to nftables, /etc/zenos/privacy/, /var/log/zenos/
  - Create `data/apparmor/zenos-resource-manager` — restrict to /sys/fs/cgroup/, /proc/pressure/, /sys/block/zram*
  - Create `data/apparmor/zenos-update-manager` — restrict to /ostree/, network, /etc/zenos/update/
  - Create `data/apparmor/zenos-package-manager` — restrict to /var/lib/flatpak/, /ostree/, network
  - Create `data/apparmor/zenos-session` — restrict to PAM, logind socket, /etc/passwd, /etc/shadow
  - Create `data/nftables/zenos-firewall.nft` — table inet filter with default-deny inbound chain, allow established/related, allow loopback, placeholder Privacy Guard outbound chain
  - Create `data/polkit/50-zenos.rules` — require auth for: org.zenos.PackageManager.*, org.zenos.UpdateManager.ApplyUpdate, org.zenos.ResourceManager.SetWaydroidEnabled
  - Create `data/zram/zenos-zram.conf` — algorithm=lz4, size=50% of RAM, streams=auto
  - Create `data/ostree/` directory for OSTree remote and update configuration (populated in Sub-Phase 4.5)
- **Quality Gate**:
  - [x] All 6 AppArmor profile files exist under `data/apparmor/`
  - [x] Each AppArmor profile contains a `profile` block with at least one file rule and one capability rule
  - [x] `data/nftables/zenos-firewall.nft` exists and contains `table inet filter` with `chain input` and `chain output`
  - [x] `data/polkit/50-zenos.rules` exists and contains at least 3 `polkit.addRule` calls
  - [x] `data/zram/zenos-zram.conf` exists and contains `ALGORITHM=lz4` and `SIZE=50`

### Sub-Phase 0.11: Branding and Icon Theme Scaffolding ✅

- **Goal**: Icon theme directory structure follows the freedesktop icon theme spec with placeholder assets.
- **Tasks**:
  - Create `data/branding/icons/index.theme` with theme metadata (Name=Zen OS, Comment, Directories listing all size/category combos)
  - Create size directories under `data/branding/icons/hicolor/`: 16x16, 24x24, 32x32, 48x48, 64x64, 128x128, 256x256, scalable
  - Create category directories under each size: apps, actions, status, devices, places
  - Add placeholder SVG for Zen OS logo at `data/branding/icons/hicolor/scalable/apps/zenos.svg`
- **Quality Gate**:
  - [x] `data/branding/icons/index.theme` exists and contains `[Icon Theme]` section with `Name=Zen OS`
  - [x] `find data/branding/icons/hicolor -type d | wc -l` returns ≥ 40 (8 sizes × 5 categories)
  - [x] `data/branding/icons/hicolor/scalable/apps/zenos.svg` exists and is valid SVG (contains `<svg` tag)

### Sub-Phase 0.12: Zen Browser Configuration Templates ✅

- **Goal**: Browser privacy-hardening config files exist under data/browser/ ready for injection at boot time.
- **Tasks**:
  - Create `data/browser/policies.json` — enterprise-style policy: disable telemetry, crash reporting, studies, default search engine, disable pocket, disable sponsored content
  - Create `data/browser/user.js` — preference overrides: disable `toolkit.telemetry.*`, `datareporting.*`, `browser.newtabpage.activity-stream.feeds.*`, `network.prefetch-next`, `browser.safebrowsing.*` (Google endpoint)
  - Create `data/browser/userChrome.css` — minimal Zen OS visual identity placeholder (custom tab bar color matching system theme)
- **Quality Gate**:
  - [x] All 3 files exist under `data/browser/`
  - [x] `python3 -m json.tool data/browser/policies.json` exits 0 (valid JSON)
  - [x] `grep -c 'DisableTelemetry' data/browser/policies.json` returns ≥ 1
  - [x] `grep -c 'toolkit.telemetry' data/browser/user.js` returns ≥ 1
  - [x] `data/browser/userChrome.css` contains at least one CSS rule

### Sub-Phase 0.13: Documentation Completion ✅

- **Goal**: docs/CONTRIBUTING.md and docs/architecture/ ADR index exist with complete content.
- **Tasks**:
  - Write `docs/CONTRIBUTING.md` — contribution guidelines: branch naming (`feature/`, `fix/`, `docs/`), commit message format (imperative, English, ≤ 72 char subject), PR process, code style (C17, K&R braces, 4-space indent, 100-col limit, snake_case), testing requirements (all tests pass, ASan clean)
  - Create `docs/architecture/README.md` — ADR index explaining the Architecture Decision Record format, listing planned ADRs (ADR-001: In-process shell, ADR-002: Cairo+Pango over GTK, ADR-003: OSTree layering over direct APT, ADR-004: SceneFX over raw wlroots scene)
- **Quality Gate**:
  - [x] `docs/CONTRIBUTING.md` exists and contains sections for branch naming, commit format, code style, and testing
  - [x] `docs/architecture/README.md` exists and lists at least 4 planned ADRs
  - [x] All documentation files are written in English

### Sub-Phase 0.14: CI Pipeline ✅

- **Goal**: GitHub Actions CI workflow builds, lints, and tests the project on every push and PR.
- **Tasks**:
  - Create `.github/workflows/ci.yml` — triggers on push and PR to main
    - Job 1: lint — run cppcheck on all `src/**/*.c` files, run clang-format --dry-run --Werror on all `src/**/*.c` and `src/**/*.h` files
    - Job 2: build — `meson setup builddir` + `meson compile -C builddir` with gcc and clang matrix
    - Job 3: test — `meson test -C builddir --verbose`
    - Cache meson builddir and subprojects for faster CI
  - Create `.clang-format` at repo root with project style settings (BasedOnStyle: LLVM, IndentWidth: 4, BreakBeforeBraces: Linux, ColumnLimit: 100)
- **Quality Gate**:
  - [x] `.github/workflows/ci.yml` exists and contains `on: [push, pull_request]` trigger
  - [x] CI workflow defines at least 3 jobs: lint, build, test
  - [x] CI build job uses matrix strategy with at least gcc and clang
  - [x] `.clang-format` exists and contains `IndentWidth: 4` and `ColumnLimit: 100`

### Sub-Phase 0.15: Scaffolding Verification Checkpoint ✅

- **Goal**: All Phase 0 artifacts are verified complete, consistent, and the build system compiles everything cleanly.
- **Tasks**:
  - Verify all directories and files from Sub-Phases 0.1–0.14 exist
  - Run `meson setup builddir --wipe` and `meson compile -C builddir` — must exit 0
  - Run `meson test -C builddir` — must exit 0 with ≥ 1 test passed
  - Run `xmllint --noout data/dbus/*.xml` — must exit 0 for all files
  - Verify `.gitignore` excludes builddir/, .kiro/, *.o, *.so
  - Verify no files exist outside the sanctioned directory structure (src/, data/, tools/, tests/, docs/)
  - Note: `tests/` may contain pre-existing test scripts from bugfix specs (e.g., `tests/scenefx-renderer/`); these are sanctioned under the `tests/` directory
  - Update `docs/ROADMAP.md` to mark Phase 0 complete
  - Update `docs/CHANGELOG.md` with Phase 0 completion entry
  - Commit all changes with message: "phase0: complete planning and project setup scaffolding"
- **Quality Gate**:
  - [x] `meson setup builddir --wipe` exits 0
  - [x] `meson compile -C builddir` exits 0 with 0 errors
  - [x] `meson test -C builddir` exits 0 with ≥ 1 test passed
  - [x] `xmllint --noout data/dbus/*.xml` exits 0
  - [x] `find data/systemd -name '*.service' -o -name '*.target' | wc -l` returns ≥ 8
  - [x] `find data/apparmor -type f | wc -l` returns ≥ 6
  - [x] `find data/branding/icons -type d | wc -l` returns ≥ 40
  - [x] `find src -name 'meson.build' | wc -l` returns ≥ 10 (all component build files)
  - [x] No unsanctioned top-level directories exist (only src/, data/, tools/, tests/, docs/, subprojects/, .github/)
  - [x] All Phase 0 sub-phases marked ✅ in this ROADMAP

---

## Phase 1: Foundation — Boot, Compositor Core, Session (v0.1.x)

**Goal**: Boot into a minimal Wayland session with window management, input routing, crash isolation, and user login.

**Status**: 🟡 In Progress (Sub-Phase 1.1 complete)

### Sub-Phase 1.1: Minimal Compositor — Empty Frame ✅

- **Goal**: wlroots + SceneFX compositor initializes, renders a solid-color frame in headless QEMU, and emits ZEN_BOOT_OK.
- **Tasks**:
  - Implement `zen_compositor_create()`, `zen_compositor_run()`, `zen_compositor_destroy()` in `src/compositor/src/main.c`
  - Implement output handlers (frame, request_state, destroy) and new_output handler
  - Emit `ZEN_BOOT_OK` to `/dev/ttyS0` serial port after successful initialization
  - Enforce `WLR_RENDERER=gles2` for SceneFX compatibility
- **Quality Gate**:
  - [x] `meson compile -C builddir` exits 0 with 0 errors, 0 warnings
  - [x] QEMU boot completes and `ZEN_BOOT_OK` appears in serial log
  - [x] Screenshot PPM file is > 1 KB (non-empty frame rendered)
  - [x] AddressSanitizer reports 0 errors
  - [x] LeakSanitizer reports 0 errors

### Sub-Phase 1.2: xdg_shell Window Lifecycle ⬜

- **Goal**: xdg_shell surfaces can be mapped, configured, resized, and closed by Wayland clients.
- **Tasks**:
  - Create `src/compositor/src/xdg.c` — implement xdg_toplevel handlers: map, unmap, request_move, request_resize, set_title, set_app_id, request_fullscreen, request_maximize
  - Create `src/compositor/include/zen/xdg.h` — xdg surface state struct and handler declarations
  - Register `wlr_xdg_shell` in compositor create, add xdg surface to scene graph
  - Implement surface focus tracking (keyboard focus follows pointer or most-recently-mapped)
  - Update `src/compositor/meson.build` to include new source files
- **Quality Gate**:
  - [ ] `weston-terminal` (or `foot`) launches as a Wayland client, displays a window, and exits cleanly
  - [ ] Window resize via client request produces correct scene graph update (no visual artifacts)
  - [ ] Closing the client window removes the surface from the scene graph
  - [ ] ASan reports 0 errors after client open/close cycle
  - [ ] `meson compile -C builddir` exits 0 with 0 errors

### Sub-Phase 1.3: Input Routing ⬜

- **Goal**: Keyboard, pointer, and touch input events are routed to the focused Wayland surface.
- **Tasks**:
  - Create `src/compositor/src/input.c` — implement keyboard, pointer, and touch handlers
  - Create `src/compositor/include/zen/input.h` — input device state structs
  - Implement seat creation with `wlr_seat`, bind keyboard and pointer to seat
  - Implement pointer focus: surface under cursor receives pointer events
  - Implement keyboard focus: focused surface receives key events via `wlr_seat_keyboard_notify_*`
  - Implement cursor rendering via `wlr_cursor` and `wlr_xcursor_manager`
- **Quality Gate**:
  - [ ] Pointer movement over a mapped xdg_toplevel surface changes the cursor icon
  - [ ] Clicking on a surface gives it keyboard focus (verified by typing in weston-terminal)
  - [ ] Key events reach the focused client (verified by typing characters in a terminal client)
  - [ ] Touch events on a surface trigger the same focus behavior as pointer click
  - [ ] ASan reports 0 errors after input interaction sequence

### Sub-Phase 1.4: Crash Isolation ⬜

- **Goal**: When a managed Wayland client crashes or disconnects, the compositor cleans up the surface and continues operating without restart.
- **Tasks**:
  - Implement client destroy handler in xdg.c — remove surface from scene graph, release focus if focused
  - Implement `wl_client` destroy listener to catch unexpected disconnects
  - Test with a client that deliberately crashes (SIGSEGV) mid-session
  - Ensure no dangling pointers or use-after-free in surface cleanup path
- **Quality Gate**:
  - [ ] Launch 2 clients, kill one with `kill -9`, the other continues receiving input and rendering
  - [ ] Compositor does not crash or log ASan errors after client kill
  - [ ] Focus transfers to the remaining client after the killed client's surface is removed
  - [ ] Repeated client launch/kill cycles (10x) produce 0 ASan errors and 0 LeakSanitizer errors

### Sub-Phase 1.5: Multi-Monitor Support ⬜

- **Goal**: Compositor supports multiple outputs with independent resolution and scaling per display.
- **Tasks**:
  - Implement per-output resolution and scale configuration in output handler
  - Implement `wlr_output_layout` positioning for multi-monitor arrangements
  - Implement cursor movement across output boundaries
  - Test with QEMU multi-head configuration (2 virtual displays)
- **Quality Gate**:
  - [ ] QEMU launched with 2 virtual displays, both render the background color
  - [ ] Each output reports correct resolution in `wlr_log` output
  - [ ] Cursor moves seamlessly between outputs
  - [ ] A window can be moved from one output to another
  - [ ] ASan reports 0 errors

### Sub-Phase 1.6: XWayland Bridge ⬜

- **Goal**: Legacy X11 applications run inside the compositor via XWayland.
- **Tasks**:
  - Initialize `wlr_xwayland` in compositor create
  - Implement xwayland surface handlers: map, unmap, configure, set_title
  - Add xwayland surfaces to scene graph alongside xdg surfaces
  - Implement focus and input routing for xwayland surfaces
- **Quality Gate**:
  - [ ] An X11 application (e.g., `xterm` or `xclock`) launches and displays inside the compositor
  - [ ] X11 window receives keyboard and pointer input
  - [ ] Closing the X11 application removes the surface cleanly
  - [ ] ASan reports 0 errors after X11 client lifecycle

### Sub-Phase 1.7: Cairo + Pango Shell Rendering Integration ⬜

- **Goal**: Cairo + Pango can render text and shapes into texture buffers that are composited into the SceneFX scene graph.
- **Tasks**:
  - Create `src/compositor/src/cairo_buffer.c` — utility to create a Cairo surface, render content, and upload as `wlr_scene_buffer` node
  - Create `src/compositor/include/zen/cairo_buffer.h` — API for creating and updating Cairo-backed scene buffers
  - Implement a test overlay: render "Zen OS" text at the bottom of the screen using Pango
  - Verify the text overlay composites correctly with SceneFX blur/shadow effects
- **Quality Gate**:
  - [ ] A text overlay reading "Zen OS" is visible in the compositor screenshot
  - [ ] The overlay is rendered via Cairo + Pango (verified by code inspection — no GTK dependency)
  - [ ] Screenshot PPM shows text at expected position (bottom-center of frame)
  - [ ] ASan reports 0 errors
  - [ ] No new pkg-config dependencies added beyond cairo and pangocairo

### Sub-Phase 1.8: Compositor D-Bus Interface ⬜

- **Goal**: Compositor exposes org.zenos.Compositor D-Bus interface for external control (app launch, shelf config, dark mode, notifications).
- **Tasks**:
  - Create `src/compositor/src/dbus.c` — implement sd-bus interface registration and method handlers
  - Create `src/compositor/include/zen/dbus.h` — D-Bus interface declarations
  - Implement methods: LaunchApp, GetRunningApps, ToggleDarkMode, ShowNotification (stubs for PinToShelf, SetShelfConfig until shell exists)
  - Add libsystemd (sd-bus) dependency to compositor meson.build
  - Connect sd-bus event loop to wl_event_loop
- **Quality Gate**:
  - [ ] `busctl introspect org.zenos.Compositor /org/zenos/Compositor` shows all methods from the XML contract
  - [ ] `busctl call org.zenos.Compositor /org/zenos/Compositor org.zenos.Compositor ToggleDarkMode b true` returns without error
  - [ ] D-Bus interface name matches `data/dbus/org.zenos.Compositor.xml` exactly
  - [ ] ASan reports 0 errors after D-Bus method calls

### Sub-Phase 1.9: Session Manager ⬜

- **Goal**: PAM authentication and systemd-logind session management allow a user to log in and start a Wayland session.
- **Tasks**:
  - Create `src/session/src/session.c` — implement PAM authentication flow, logind session creation via sd-bus, compositor launch
  - Create `src/session/include/zen/session.h` — session manager API
  - Implement first-boot detection: check `/var/lib/zenos/oobe-complete` flag
  - Implement logout: terminate all user processes via `loginctl terminate-session`
  - Add PAM and libsystemd dependencies to session meson.build
- **Quality Gate**:
  - [ ] Session manager authenticates a test user via PAM (verified by log output)
  - [ ] A logind session is created (verified by `loginctl list-sessions` showing the session)
  - [ ] Compositor starts after successful authentication
  - [ ] Logout terminates all user processes and releases the session
  - [ ] ASan reports 0 errors

### Sub-Phase 1.10: OSTree Boot Health Check ⬜

- **Goal**: On boot, the system verifies the current OSTree deployment is healthy and automatically rolls back to the previous deployment on failure.
- **Tasks**:
  - Create `src/daemons/boot-check/` or implement as a shell script at `/usr/libexec/zenos-boot-check`
  - Implement deployment health check: verify critical binaries exist, verify systemd services started, verify compositor process is running
  - Implement automatic rollback via `ostree admin undeploy` + reboot if health check fails
  - Integrate with `data/systemd/zenos-boot-check.service` (oneshot, early boot)
- **Quality Gate**:
  - [ ] Health check script/binary exits 0 on a healthy deployment
  - [ ] Health check exits non-zero when a critical binary is missing (simulated by renaming)
  - [ ] On health check failure, the system rolls back to the previous OSTree deployment
  - [ ] Boot counter mechanism prevents infinite rollback loops (max 3 attempts)

### Sub-Phase 1.11: Memory Budget Validation ⬜

- **Goal**: Compositor core idle RSS ≤ 30 MB, verified on reference hardware.
- **Tasks**:
  - Measure compositor RSS after boot using `/proc/<pid>/status` VmRSS field
  - Profile memory usage with Valgrind massif on headless QEMU
  - Identify and fix any memory bloat (unnecessary allocations, leaked buffers)
  - Document memory baseline in CHANGELOG.md
- **Quality Gate**:
  - [ ] `grep VmRSS /proc/$(pidof zen-compositor)/status` reports ≤ 30720 kB (30 MB) on QEMU reference hardware
  - [ ] Valgrind massif peak heap usage ≤ 20 MB
  - [ ] No memory leaks reported by LeakSanitizer after 60 seconds of idle operation

### Sub-Phase 1.12: Desktop Wallpaper Rendering ⬜

- **Goal**: Compositor renders a configurable desktop wallpaper behind all windows and shell elements.
- **Tasks**:
  - Implement wallpaper loading from `data/branding/wallpaper/` (PNG or JPEG via Cairo image surface)
  - Add a default wallpaper asset to `data/branding/wallpaper/default.png`
  - Render wallpaper as the bottom-most `wlr_scene_buffer` node in the scene graph, scaled to output resolution
  - Implement solid-color fallback if wallpaper file is missing
  - Persist wallpaper choice to `~/.config/zenos/theme.json`
- **Quality Gate**:
  - [ ] Screenshot shows wallpaper image behind all windows (not just a solid color)
  - [ ] Wallpaper scales correctly to output resolution without distortion
  - [ ] Missing wallpaper file falls back to solid color without crash
  - [ ] ASan reports 0 errors

### Sub-Phase 1.13: Global Keybinding System ⬜

- **Goal**: Compositor intercepts configurable global keyboard shortcuts before they reach focused clients.
- **Tasks**:
  - Create `src/compositor/src/keybinds.c` — keybinding registry and dispatch
  - Create `src/compositor/include/zen/keybinds.h` — keybinding API
  - Implement default bindings: Ctrl+Alt+T (terminal), Super (App Launcher toggle), Super+L (screen lock), Alt+Tab (window switch), Super+Q (close window)
  - Implement keybinding config from `~/.config/zenos/keybinds.json` (override defaults)
  - Keybindings are consumed by compositor and NOT forwarded to focused client
- **Quality Gate**:
  - [ ] Pressing a bound key combination triggers the registered action (verified by log output)
  - [ ] Bound keys are NOT received by the focused Wayland client
  - [ ] Custom keybinding from `keybinds.json` overrides the default
  - [ ] ASan reports 0 errors

### Sub-Phase 1.14: wlr-layer-shell Protocol ⬜

- **Goal**: Compositor supports the wlr-layer-shell protocol for overlay surfaces (screen lock, external panels, screenshot tools).
- **Tasks**:
  - Register `wlr_layer_shell_v1` in compositor create
  - Implement layer surface handlers: map, unmap, configure (anchor, margin, exclusive zone)
  - Support all 4 layers: background, bottom, top, overlay
  - Implement exclusive zone: layer surfaces can reserve screen edges (e.g., for external bars)
- **Quality Gate**:
  - [ ] A layer-shell client (e.g., `wlr-randr` or a test client) creates a surface on the overlay layer
  - [ ] Layer surface with exclusive zone reserves screen space (windows do not overlap it)
  - [ ] Layer surfaces on the overlay layer render above all xdg_toplevel windows
  - [ ] ASan reports 0 errors

### Sub-Phase 1.15: Screen Lock ⬜

- **Goal**: A screen lock mechanism blanks the display and requires PAM re-authentication to unlock.
- **Tasks**:
  - Create `src/compositor/src/lock.c` — implement `ext-session-lock-v1` Wayland protocol
  - Create `src/compositor/include/zen/lock.h` — lock state and API
  - Render lock screen via Cairo + Pango: clock, user avatar placeholder, password field
  - Trigger lock via: Super+L keybinding, idle timeout (configurable), lid close (via logind)
  - Authenticate unlock via PAM (same stack as session manager)
  - While locked: all input is captured by lock screen, no client receives events
- **Quality Gate**:
  - [ ] Super+L activates the lock screen, blanking all outputs
  - [ ] While locked, keyboard and pointer input do NOT reach any client
  - [ ] Entering the correct password unlocks the session and restores the desktop
  - [ ] Entering an incorrect password shows an error message and remains locked
  - [ ] ASan reports 0 errors

---

## Phase 2: Desktop Shell (v0.2.x)

**Goal**: ChromeOS-inspired desktop shell with Shelf, App Launcher, Quick Settings, notifications, and theming — all rendered in-process via Cairo + Pango with SceneFX visual effects.

**Status**: ⬜ Not Started

> **Note**: PipeWire and WirePlumber are provided by the Debian base image and are not compiled as part of Zen OS. The shell integrates with them via D-Bus (`org.freedesktop.portal.*`, `wp_mixer_api`). Their systemd user units are started by the Debian-provided packages.

### Sub-Phase 2.1: Shelf — Basic Rendering ⬜

- **Goal**: A persistent bottom bar renders on the primary display using Cairo + Pango, composited into the SceneFX scene graph.
- **Tasks**:
  - Create `src/shell/src/shelf.c` — render a horizontal bar at the bottom of the primary output using Cairo
  - Implement shelf as a `wlr_scene_buffer` node with SceneFX rounded corners (≥ 8px) and drop shadow
  - Render placeholder app icons (colored circles) and a clock in the system tray area using Pango
  - Shelf height: 48px default, full width of primary output
- **Quality Gate**:
  - [ ] Screenshot shows a horizontal bar at the bottom of the screen with rounded corners
  - [ ] Bar has visible drop shadow (SceneFX effect applied)
  - [ ] At least one text element (clock) is rendered via Pango
  - [ ] Shelf does not overlap or obscure xdg_toplevel windows (windows are constrained above shelf)
  - [ ] ASan reports 0 errors

### Sub-Phase 2.2: Shelf — Pinned Apps and Running Indicators ⬜

- **Goal**: Shelf displays pinned application icons and indicates which applications are currently running.
- **Tasks**:
  - Implement icon loading from freedesktop icon theme (data/branding/icons/)
  - Implement pinned app list from `~/.config/zenos/shelf.json`
  - Render running indicator (dot below icon) for apps with active xdg_toplevel surfaces
  - Implement click-to-launch: clicking a pinned icon launches the app via `exec`
  - Implement click-to-focus: clicking a running app icon focuses its surface
- **Quality Gate**:
  - [ ] Shelf displays at least 3 pinned app icons loaded from shelf.json
  - [ ] Running apps show a dot indicator below their icon
  - [ ] Clicking a pinned icon launches the application (verified by new surface appearing)
  - [ ] Clicking a running app icon focuses its surface
  - [ ] ASan reports 0 errors

### Sub-Phase 2.3: Shelf — System Tray ⬜

- **Goal**: System tray area displays battery, Bluetooth, network, volume, and removable storage indicators via D-Bus.
- **Tasks**:
  - Implement battery indicator: read UPower D-Bus (`org.freedesktop.UPower`) for charge percentage and charging status
  - Implement Bluetooth indicator: read BlueZ D-Bus (`org.bluez`) for connected device count
  - Implement network indicator: read NetworkManager D-Bus (`org.freedesktop.NetworkManager`) for connection state
  - Implement volume indicator: read PipeWire default sink volume
  - Implement removable storage indicator: monitor udisks2 D-Bus for mounted removable devices
  - Render all indicators as small icons with optional text labels in the tray area
- **Quality Gate**:
  - [ ] Battery icon displays with percentage text (or "N/A" if no battery detected)
  - [ ] Network icon reflects current connection state (connected/disconnected)
  - [ ] Volume icon is present in the tray
  - [ ] All D-Bus connections are established without errors in log
  - [ ] ASan reports 0 errors

### Sub-Phase 2.4: Shelf — Configuration and Context Menu ⬜

- **Goal**: Shelf configuration (position, auto-hide, icon size) persists across reboots, and right-click shows a context menu.
- **Tasks**:
  - Implement shelf config persistence to `~/.config/zenos/shelf.json` (read on start, write on change)
  - Implement drag-to-pin: dragging an app icon onto the shelf pins it
  - Implement right-click context menu: Cairo-rendered popup with options for position (bottom/left/right/top), auto-hide toggle, icon size slider
  - Implement auto-hide behavior: shelf slides off-screen after timeout, reappears on pointer proximity
- **Quality Gate**:
  - [ ] `~/.config/zenos/shelf.json` is created on first run with default values
  - [ ] Changing icon size via context menu persists after compositor restart
  - [ ] Right-click on shelf shows a popup menu with at least 3 options
  - [ ] Auto-hide: shelf disappears after 3 seconds of no pointer proximity, reappears when pointer moves to screen edge
  - [ ] ASan reports 0 errors

### Sub-Phase 2.5: App Launcher ⬜

- **Goal**: Fullscreen overlay grid displays all installed applications with real-time search filtering.
- **Tasks**:
  - Create `src/shell/src/app-launcher.c` — fullscreen overlay rendered via Cairo + Pango with SceneFX translucent Gaussian blur background
  - Scan `.desktop` files from `/usr/share/applications/`, Flatpak exports (`/var/lib/flatpak/exports/share/applications/`), and `~/.local/share/applications/` (PWAs)
  - Render application grid with icon + name for each app
  - Implement search field at top: real-time filtering by app name and keywords as user types
  - Implement keyboard navigation: arrow keys to move selection, Enter to launch, Escape to close
  - Trigger from shelf: clicking the App Launcher button on the shelf opens/closes the overlay
- **Quality Gate**:
  - [ ] App Launcher overlay covers the full screen with translucent blur background
  - [ ] At least 3 installed applications are displayed in a grid layout
  - [ ] Typing in the search field filters the grid in real time
  - [ ] Pressing Enter on a selected app launches it (new surface appears)
  - [ ] Pressing Escape closes the overlay
  - [ ] Keyboard navigation (arrow keys) moves the selection highlight
  - [ ] ASan reports 0 errors

### Sub-Phase 2.6: Quick Settings Panel ⬜

- **Goal**: Popup panel from system tray provides toggles for Wi-Fi, Bluetooth, volume, brightness, and Do Not Disturb.

> **Note**: This sub-phase implements the Quick Settings UI shell and basic toggle actions (on/off via D-Bus). Full Wi-Fi network listing, credential management, VPN support, and DNS-over-HTTPS configuration are implemented in Sub-Phase 4.7 (Network Integration).
- **Tasks**:
  - Create `src/shell/src/quick-settings.c` — popup panel rendered via Cairo + Pango with SceneFX blur and rounded corners
  - Implement Wi-Fi toggle: call NetworkManager D-Bus to enable/disable wireless
  - Implement Bluetooth toggle: call BlueZ D-Bus to power on/off adapter
  - Implement volume slider: control PipeWire default sink volume via `wp_mixer_api`
  - Implement brightness slider: write to `/sys/class/backlight/*/brightness`
  - Implement Do Not Disturb toggle: suppress notification toasts when enabled
  - Implement per-app volume control in expanded view (list PipeWire streams with individual sliders)
  - Trigger from shelf: clicking the system tray area opens/closes the panel
- **Quality Gate**:
  - [ ] Quick Settings panel appears as a popup anchored to the system tray area
  - [ ] Panel has translucent blur background and rounded corners (≥ 8px)
  - [ ] Wi-Fi toggle changes NetworkManager wireless state (verified by `nmcli radio wifi`)
  - [ ] Volume slider changes PipeWire sink volume (verified by `wpctl get-volume @DEFAULT_AUDIO_SINK@`)
  - [ ] Clicking outside the panel closes it
  - [ ] ASan reports 0 errors

### Sub-Phase 2.7: Notification Manager ⬜

- **Goal**: Desktop notifications display as floating toast elements implementing the org.freedesktop.Notifications D-Bus interface.
- **Tasks**:
  - Create `src/shell/src/notifications.c` — implement `org.freedesktop.Notifications` D-Bus interface via sd-bus
  - Render notification toasts as Cairo surfaces anchored to top-right of primary display
  - Implement toast lifecycle: appear with fade-in, auto-dismiss after timeout, manual dismiss on click
  - Style consistently with system theme (dark/light mode), drop shadows via SceneFX
  - Implement notification queue: stack multiple notifications vertically
- **Quality Gate**:
  - [ ] `notify-send "Test" "Hello from Zen OS"` displays a toast notification in the top-right corner
  - [ ] Toast auto-dismisses after the default timeout (5 seconds)
  - [ ] Multiple notifications stack vertically without overlapping
  - [ ] Toast styling matches the current dark/light mode
  - [ ] ASan reports 0 errors

### Sub-Phase 2.8: Dark Mode / Light Mode ⬜

- **Goal**: System-wide dark mode and light mode toggle applied consistently to all shell elements.
- **Tasks**:
  - Implement theme state (dark/light) stored in `~/.config/zenos/theme.json`
  - Implement `ToggleDarkMode` D-Bus method to switch theme at runtime
  - Update Shelf, App Launcher, Quick Settings, and Notification Manager to read theme state and use appropriate color palette
  - Implement SceneFX visual adjustments per theme (shadow intensity, blur tint)
  - Persist theme choice across reboots
- **Quality Gate**:
  - [ ] `busctl call org.zenos.Compositor ... ToggleDarkMode b true` switches all shell elements to dark palette
  - [ ] `busctl call org.zenos.Compositor ... ToggleDarkMode b false` switches all shell elements to light palette
  - [ ] Theme persists after compositor restart (read from theme.json)
  - [ ] Screenshot comparison: dark mode screenshot has measurably lower average pixel brightness than light mode
  - [ ] ASan reports 0 errors

---

## Phase 3: Application Ecosystem (v0.3.x)

**Goal**: Zen Browser integration, Flatpak support, OSTree package layering, PWAs, and a bundled terminal emulator.

**Status**: ⬜ Not Started

### Sub-Phase 3.1: Zen Browser Privacy Hardening and Integration ⬜

- **Goal**: Zen Browser launches with zero telemetry via config injection, auto-starts on session, and supports crash recovery.
- **Tasks**:
  - Implement browser config injection: symlink `data/browser/policies.json`, `user.js`, `userChrome.css` into browser profile on session start
  - Implement auto-launch: compositor starts Zen Browser as the first application after session init
  - Implement crash recovery: detect browser exit, offer restart dialog via Cairo, restore session from `sessionstore.jsonlz4`
- **Quality Gate**:
  - [ ] Zen Browser launches with `about:telemetry` showing all telemetry disabled
  - [ ] `policies.json` is active (verified by `about:policies` page)
  - [ ] Browser auto-launches on session start without user intervention
  - [ ] After simulated crash (kill -9), compositor shows restart dialog and browser restores previous tabs
  - [ ] Zero outbound connections to `*.mozilla.org/telemetry*` endpoints (verified by tcpdump during test session)

### Sub-Phase 3.2: PWA Registration ⬜

- **Goal**: PWAs installed through Zen Browser appear in the App Launcher as launchable applications.
- **Tasks**:
  - Implement PWA install hook: monitor `~/.local/share/applications/` for new `.desktop` files created by browser PWA install
  - Register PWA in App Launcher's application list
  - Implement PWA icon extraction from browser cache
  - PWAs launch in their own window (browser `--app` mode)
- **Quality Gate**:
  - [ ] Installing a PWA in Zen Browser creates a `.desktop` file in `~/.local/share/applications/`
  - [ ] The PWA appears in the App Launcher grid
  - [ ] Launching the PWA from App Launcher opens it in a standalone window (no browser chrome)
  - [ ] PWA icon is displayed correctly in both App Launcher and Shelf

### Sub-Phase 3.3: Flatpak Package Management ⬜

- **Goal**: Package Manager daemon supports installing, removing, and listing Flatpak applications with sandbox enforcement.
- **Tasks**:
  - Implement `src/daemons/package-manager/src/package-manager.c` — D-Bus daemon using libflatpak
  - Implement methods: InstallFlatpak, RemoveFlatpak, ListInstalled, Search
  - Implement Flatpak sandbox enforcement: restricted permissions by default
  - Implement permission prompting: when a Flatpak requests portal access, prompt user via Cairo dialog
  - Register D-Bus service with systemd activation
- **Quality Gate**:
  - [ ] `busctl call org.zenos.PackageManager ... InstallFlatpak ss "org.gnome.Calculator" "flathub"` installs the app
  - [ ] `busctl call org.zenos.PackageManager ... ListInstalled` returns the installed app
  - [ ] `flatpak info org.gnome.Calculator` shows the app is installed with restricted permissions
  - [ ] `busctl call org.zenos.PackageManager ... RemoveFlatpak s "org.gnome.Calculator"` removes the app
  - [ ] ASan reports 0 errors

### Sub-Phase 3.4: OSTree Native Package Layering ⬜

- **Goal**: Package Manager supports layering native Debian packages onto the OSTree deployment and removing them.
- **Tasks**:
  - Implement LayerPackage method: `ostree admin deploy` with package overlay, produce new deployment
  - Implement UnlayerPackage method: new deployment without the package
  - Implement conflict detection: reject if package conflicts with base image
  - Block direct APT modification of read-only root (verify root is mounted read-only)
- **Quality Gate**:
  - [ ] `busctl call org.zenos.PackageManager ... LayerPackage s "htop"` produces a new OSTree deployment containing htop
  - [ ] `ostree admin status` shows the new deployment with the layered package
  - [ ] `busctl call org.zenos.PackageManager ... UnlayerPackage s "htop"` produces a deployment without htop
  - [ ] `apt install vim` fails with a read-only filesystem error (direct APT blocked)
  - [ ] Conflicting package installation is rejected with a descriptive error message

### Sub-Phase 3.5: Terminal Emulator ⬜

- **Goal**: A minimal terminal emulator is bundled as a native application.
- **Tasks**:
  - Evaluate lightweight terminal options: foot (Wayland-native, minimal dependencies) as bundled terminal
  - Package foot as a Flatpak or include in base image
  - Register terminal in App Launcher with appropriate icon and keyboard shortcut (Ctrl+Alt+T)
- **Quality Gate**:
  - [ ] Terminal emulator launches from App Launcher
  - [ ] Terminal accepts keyboard input and displays shell output
  - [ ] Ctrl+Alt+T keyboard shortcut opens a new terminal window (requires Sub-Phase 1.13 keybinding system)
  - [ ] Terminal appears in Shelf running indicators when active

### Sub-Phase 3.6: xdg-desktop-portal Integration ⬜

- **Goal**: xdg-desktop-portal-wlr is configured so Flatpak apps can access file chooser, screenshot, and screen sharing portals.
- **Tasks**:
  - Configure xdg-desktop-portal and xdg-desktop-portal-wlr as Debian base packages (no custom build)
  - Create `data/xdg-desktop-portal/zenos-portals.conf` — portal backend configuration mapping
  - Verify file chooser portal works for Flatpak apps (open/save dialogs)
  - Verify screenshot portal works (PipeWire screen capture)
  - Verify screen sharing portal works (PipeWire + wlr-screencopy)
- **Quality Gate**:
  - [ ] A Flatpak app (e.g., GNOME Calculator) can open a file chooser dialog via portal
  - [ ] `xdg-desktop-portal` and `xdg-desktop-portal-wlr` processes are running
  - [ ] Screenshot via portal produces a valid image file
  - [ ] No portal-related errors in `journalctl --user -u xdg-desktop-portal`

---

## Phase 4: System Services (v0.4.x)

**Goal**: Privacy Guard, Resource Manager, Update Manager, and Network Manager are operational system daemons.

**Status**: ⬜ Not Started

### Sub-Phase 4.1: Privacy Guard — nftables Telemetry Blocking ⬜

- **Goal**: Privacy Guard daemon blocks all known telemetry and tracking domains via nftables rules.
- **Tasks**:
  - Implement `src/daemons/privacy-guard/src/privacy-guard.c` — systemd D-Bus daemon
  - Load telemetry blocklist from `/etc/zenos/privacy/blocklist.conf`
  - Generate and apply nftables rules to drop outbound connections to blocklisted domains/IPs
  - Implement audit logging to `/var/log/zenos/privacy-guard.log`
- **Quality Gate**:
  - [ ] `nft list ruleset` shows the Privacy Guard outbound blocking chain with ≥ 50 blocked domains
  - [ ] `curl -s https://telemetry.mozilla.org` times out or is rejected (blocked by nftables)
  - [ ] `/var/log/zenos/privacy-guard.log` contains at least one blocked request entry
  - [ ] ASan reports 0 errors

### Sub-Phase 4.2: Privacy Guard — DNS Filtering and D-Bus Interface ⬜

- **Goal**: DNS-level filtering as secondary blocking layer, plus full D-Bus interface for audit log access.
- **Tasks**:
  - Implement DNS-level filtering via systemd-resolved custom configuration
  - Implement D-Bus methods: GetBlockedRequests, GetAuditLog, ReloadRules
  - Implement RequestBlocked signal emitted on each blocked request
- **Quality Gate**:
  - [ ] DNS queries for blocked domains return NXDOMAIN or are dropped
  - [ ] `busctl call org.zenos.PrivacyGuard ... GetAuditLog` returns audit entries
  - [ ] `busctl call org.zenos.PrivacyGuard ... ReloadRules` reloads the blocklist without restart
  - [ ] ASan reports 0 errors

### Sub-Phase 4.3: Update Manager — OSTree Delta Downloads and Verification ⬜

- **Goal**: Update Manager downloads delta updates, verifies GPG signatures, and deploys atomically.
- **Tasks**:
  - Implement `src/daemons/update-manager/src/update-manager.c` — systemd D-Bus daemon using libostree
  - Implement CheckForUpdates: query remote for new commits, calculate delta size
  - Implement ApplyUpdate: download delta, verify GPG signature, stage new deployment
  - Implement GetDeployments: list current and previous deployments
  - Maintain ≥ 2 deployments at all times
- **Quality Gate**:
  - [ ] `busctl call org.zenos.UpdateManager ... CheckForUpdates` returns update info or "up to date"
  - [ ] A staged update produces a new deployment visible in `ostree admin status`
  - [ ] GPG signature verification failure rejects the update with `ZEN_ERR_PERMISSION_DENIED`
  - [ ] At least 2 deployments exist after update (verified by `ostree admin status | grep -c '^\*\|^ '`)
  - [ ] ASan reports 0 errors

### Sub-Phase 4.4: Update Manager — Rollback and Config Preservation ⬜

- **Goal**: Manual rollback works, and /etc + /home are preserved across updates via OSTree 3-way merge.
- **Tasks**:
  - Implement Rollback method: revert to previous deployment, schedule for next boot
  - Implement /etc preservation: OSTree 3-way merge for user-modified config files
  - Implement security update prioritization: flag security updates, emit notification signal
  - Implement GetSecurityUpdates method
- **Quality Gate**:
  - [ ] `busctl call org.zenos.UpdateManager ... Rollback` schedules the previous deployment for next boot
  - [ ] After rollback + reboot, the system boots into the previous deployment
  - [ ] A user-modified file in /etc survives an update (verified by content comparison before/after)
  - [ ] /home contents are unchanged after update
  - [ ] Security updates are flagged with `is_security=true` in CheckForUpdates response

### Sub-Phase 4.5: OSTree Remote and Update Infrastructure ⬜

- **Goal**: OSTree remote URL, GPG key, and update server configuration are in place so the Update Manager can fetch real updates.
- **Tasks**:
  - Create `data/ostree/zenos.remote.conf` — OSTree remote definition with URL (GitHub Releases or Cloudflare Pages) and GPG key fingerprint
  - Generate and store project GPG signing key (public key bundled in base image at `/etc/ostree/trusted.gpg.d/`)
  - Implement remote registration in installer: `ostree remote add` during install
  - Create `data/ostree/zenos-update.conf` — update check interval, auto-download policy
  - Document update server setup in `docs/ADMIN_GUIDE.md` (static file hosting)
- **Quality Gate**:
  - [ ] `ostree remote list` shows the `zenos` remote with correct URL
  - [ ] `ostree remote show-url zenos` returns a valid HTTPS URL
  - [ ] GPG public key exists at `/etc/ostree/trusted.gpg.d/zenos.gpg`
  - [ ] `data/ostree/zenos.remote.conf` exists with remote URL and GPG key ID

### Sub-Phase 4.6: Resource Manager — zram and PSI Monitoring ⬜

- **Goal**: Resource Manager configures zram at boot and monitors memory pressure via PSI.
- **Tasks**:
  - Implement `src/daemons/resource-manager/src/resource-manager.c` — systemd D-Bus daemon
  - Configure zram at startup: lz4 compression, size = 50% of physical RAM
  - Implement PSI monitoring: poll `/proc/pressure/memory` at 1-second intervals
  - Implement pressure thresholds: `some > 25µs/1s` → freeze low-priority processes, `full > 50µs/1s` → terminate lowest-priority
  - Implement GetMemoryPressure D-Bus method
- **Quality Gate**:
  - [ ] `cat /sys/block/zram0/comp_algorithm` shows `lz4`
  - [ ] `cat /sys/block/zram0/disksize` shows approximately 50% of physical RAM
  - [ ] `busctl call org.zenos.ResourceManager ... GetMemoryPressure` returns valid PSI values
  - [ ] Under simulated memory pressure (stress-ng), the daemon logs freeze/thaw actions
  - [ ] ASan reports 0 errors

### Sub-Phase 4.7: Resource Manager — Waydroid Container Management ⬜

- **Goal**: Resource Manager controls Waydroid container lifecycle with cgroups v2 limits and freeze/thaw on focus change.
- **Tasks**:
  - Implement RAM detection: read `/proc/meminfo` MemTotal, disable Waydroid if < 4 GB
  - Implement SetWaydroidEnabled: create/destroy cgroups v2 slice for Waydroid
  - Implement FreezeContainer/ThawContainer: write to cgroup freezer
  - Implement focus-based freeze/thaw: compositor notifies Resource Manager on focus change
  - Implement GetResourceBudgets method
- **Quality Gate**:
  - [ ] On a system with < 4 GB RAM, `busctl call ... SetWaydroidEnabled b true` returns `ZEN_ERR_NOT_SUPPORTED`
  - [ ] On a system with ≥ 4 GB RAM, Waydroid container starts with cgroups v2 memory limit enforced
  - [ ] Switching focus away from Waydroid freezes the container (verified by `cat /sys/fs/cgroup/zenos/waydroid/cgroup.freeze` = 1)
  - [ ] Switching focus back thaws the container within 2 seconds
  - [ ] ASan reports 0 errors

### Sub-Phase 4.8: Network Integration ⬜

- **Goal**: Quick Settings Wi-Fi panel shows full network list with credential management, DNS-over-HTTPS is enabled, VPN support is functional, and connection loss triggers notification.

> **Note**: Sub-Phase 2.6 implements the Quick Settings UI and basic toggles. This sub-phase extends it with full network management capabilities.
- **Tasks**:
  - Implement Quick Settings Wi-Fi panel: list available networks via NetworkManager D-Bus, connect with credentials
  - Enable DNS-over-HTTPS via systemd-resolved configuration (`/etc/systemd/resolved.conf.d/zenos-doh.conf`)
  - Implement VPN support: WireGuard (kernel module) and OpenVPN (userspace) via NetworkManager
  - Implement connection loss detection: monitor NetworkManager state changes, display notification toast
  - Store Wi-Fi credentials in local keyring (libsecret)
- **Quality Gate**:
  - [ ] Quick Settings shows available Wi-Fi networks
  - [ ] Connecting to a Wi-Fi network stores credentials and auto-connects on next boot
  - [ ] `resolvectl status` shows DNS-over-HTTPS is active
  - [ ] Disconnecting the network cable triggers a notification toast
  - [ ] VPN connection via WireGuard establishes successfully

---

## Phase 5: Android Support (v0.5.x)

**Goal**: Waydroid integration for Android app compatibility with resource isolation.

**Status**: ⬜ Not Started

### Sub-Phase 5.1: Waydroid Initialization and Window Management ⬜

- **Goal**: Waydroid starts with an Android system image and Android app windows are managed by the compositor.
- **Tasks**:
  - Implement Waydroid initialization: download/configure Android system image, start LXC container
  - Implement Waydroid surface management: Android windows appear as managed compositor surfaces
  - Implement error handling: descriptive errors if Waydroid fails to start, troubleshooting guidance
- **Quality Gate**:
  - [ ] Waydroid container starts and an Android home screen is visible in a compositor window
  - [ ] Android app windows can be moved and resized like native windows
  - [ ] If Waydroid fails to start, a descriptive error message is displayed (not a crash)
  - [ ] ASan reports 0 errors

### Sub-Phase 5.2: Waydroid Resource Isolation and Lifecycle ⬜

- **Goal**: Waydroid runs with cgroups v2 resource limits, freezes on focus-away, thaws on focus-back, and can be fully uninstalled.
- **Tasks**:
  - Integrate with Resource Manager: cgroups v2 memory and CPU limits on Waydroid container
  - Implement freeze-on-focus-away: Resource Manager freezes container when user switches to native app
  - Implement thaw-on-focus-back: Resource Manager thaws container within 2 seconds
  - Implement clipboard sharing between Android and native apps
  - Implement Waydroid disable/uninstall: remove container and images without affecting core system
  - Implement 4 GB RAM gate: prevent Waydroid activation on systems with < 4 GB RAM
- **Quality Gate**:
  - [ ] `cat /sys/fs/cgroup/zenos/waydroid/memory.max` shows enforced memory limit
  - [ ] Switching to a native app freezes Waydroid (cgroup.freeze = 1)
  - [ ] Switching back thaws Waydroid within 2 seconds (measured by timestamp comparison)
  - [ ] Clipboard copy in Android → paste in native app works
  - [ ] Uninstalling Waydroid removes all container data and the system functions normally
  - [ ] On < 4 GB RAM system, Waydroid activation is blocked with a descriptive message

---

## Phase 6: Security Hardening (v0.6.x)

**Goal**: AppArmor confinement, firewall enforcement, LUKS encryption, polkit rules — defense in depth.

**Status**: ⬜ Not Started

### Sub-Phase 6.1: AppArmor Profile Enforcement ⬜

- **Goal**: All bundled system services run under AppArmor mandatory access control profiles.
- **Tasks**:
  - Finalize and test AppArmor profiles from `data/apparmor/` for all 6 services
  - Enable AppArmor enforcement mode for all profiles
  - Verify each service operates correctly under confinement
  - Test that confined services cannot access resources outside their profile
- **Quality Gate**:
  - [ ] `aa-status` shows all 6 Zen OS profiles in enforce mode
  - [ ] Each service starts and operates normally under AppArmor confinement
  - [ ] Attempting to access a restricted path from a confined service is denied and logged in `/var/log/audit/audit.log`
  - [ ] No AppArmor denials during normal operation (clean audit log)

### Sub-Phase 6.2: Firewall and polkit Enforcement ⬜

- **Goal**: nftables firewall is active with default-deny inbound, and polkit gates all privilege escalation.
- **Tasks**:
  - Activate `data/nftables/zenos-firewall.nft` at boot via systemd
  - Verify default-deny inbound policy blocks unsolicited connections
  - Activate `data/polkit/50-zenos.rules`
  - Verify polkit requires authentication for package install, system update, Waydroid enable, firewall changes
- **Quality Gate**:
  - [ ] `nft list ruleset` shows the Zen OS firewall with default-deny inbound chain
  - [ ] External port scan (nmap from another host) shows all ports filtered/closed
  - [ ] `pkexec` or D-Bus method call requiring auth prompts for password
  - [ ] Unauthenticated D-Bus calls to privileged methods return `ZEN_ERR_PERMISSION_DENIED`

### Sub-Phase 6.3: Installer — Partitioning, OSTree Deployment, and LUKS Setup ⬜

- **Goal**: The installer (`src/installer/`) performs disk partitioning, OSTree initial deployment, optional LUKS encryption, and bootloader installation on a blank disk.
- **Tasks**:
  - Implement `src/installer/src/installer.c` — disk partitioning (GPT layout), filesystem creation (ext4 for /home, ext4 or btrfs for OSTree sysroot)
  - Implement OSTree initial deployment: pull from local compose, deploy to target disk
  - Implement optional LUKS encryption: encrypt root partition during install, configure initramfs for passphrase prompt
  - Implement bootloader installation: install and configure GRUB for OSTree boot
  - Implement progress reporting via D-Bus or stdout for OOBE integration
  - Create `/var` skeleton directories: `/var/log/zenos/`, `/var/lib/zenos/`, `/var/lib/flatpak/`, `/var/cache/zenos/`, `/var/lib/waydroid/`
- **Quality Gate**:
  - [ ] Installer produces a working OSTree deployment on a blank 32 GB virtio disk in QEMU
  - [ ] `ostree admin status` on the installed system shows exactly 1 deployment
  - [ ] Installed system boots to compositor without manual intervention
  - [ ] With LUKS enabled, boot prompts for passphrase and unlocks successfully
  - [ ] `/var/log/zenos/` and `/var/lib/zenos/` directories exist on the installed system
  - [ ] ASan reports 0 errors during installer execution

### Sub-Phase 6.4: LUKS Encryption and OSTree Read-Only Root Verification ⬜

- **Goal**: Full-disk encryption and OSTree read-only root are verified end-to-end on an installed system (requires Sub-Phase 6.3 installer).
- **Tasks**:
  - Verify OSTree read-only root: `mount | grep ' / '` shows `ro` flag
  - Verify direct filesystem modification attempts fail (e.g., `touch /usr/test` returns EROFS)
  - Test LUKS unlock flow during boot (passphrase prompt in initramfs)
  - Test LUKS with wrong passphrase (must fail gracefully, not hang)
- **Quality Gate**:
  - [ ] After LUKS-enabled install, boot prompts for passphrase and unlocks successfully
  - [ ] `mount | grep ' / '` shows read-only mount
  - [ ] `touch /usr/test` fails with "Read-only file system" error
  - [ ] `lsblk -f` shows LUKS-encrypted partitions
  - [ ] Wrong passphrase attempt shows error and re-prompts (does not hang)

---

## Phase 7: Polish & Accessibility (v0.7.x)

**Goal**: OOBE wizard, accessibility features, configuration persistence, headless mode, and kiosk mode.

**Status**: ⬜ Not Started

### Sub-Phase 7.1: OOBE Wizard ⬜

- **Goal**: First-boot wizard guides the user through locale, timezone, keyboard, user account, and network setup.
- **Tasks**:
  - Implement `src/oobe/src/oobe.c` — Cairo + Pango rendered wizard running as a Wayland client
  - Implement step sequence: locale → timezone → keyboard layout → user account (username + password) → network (optional Wi-Fi) → confirm
  - Implement first-boot detection: launch when `/var/lib/zenos/oobe-complete` does not exist
  - On completion: write config to /etc, create user via `useradd`, set oobe-complete flag, reboot into user session
  - Implement power-loss recovery: if flag not set, OOBE restarts on next boot
  - Implement keyboard-only navigation for accessibility
- **Quality Gate**:
  - [ ] OOBE launches automatically on first boot (oobe-complete flag absent)
  - [ ] All 5 configuration steps are navigable and completable
  - [ ] OOBE completes without network connection (offline mode)
  - [ ] After completion, `/var/lib/zenos/oobe-complete` exists and user can log in
  - [ ] Simulated power loss (kill OOBE process) → OOBE restarts on next boot
  - [ ] All steps are navigable using only keyboard (Tab, Enter, arrow keys)
  - [ ] No cloud account prompts appear during OOBE

### Sub-Phase 7.2: Accessibility — Screen Reader and Display Modes ⬜

- **Goal**: AT-SPI2 screen reader integration, system-wide font scaling, and high-contrast mode are functional.
- **Tasks**:
  - Integrate AT-SPI2 framework: compositor exposes accessibility tree for shell elements
  - Implement system-wide font scaling: Pango font size multiplier applied to all shell text rendering
  - Implement high-contrast display mode: alternative color palette with maximum contrast ratios
  - Persist accessibility settings to `~/.config/zenos/accessibility.json`
  - Implement accessibility settings panel in Quick Settings
- **Quality Gate**:
  - [ ] Orca screen reader (or equivalent) can read Shelf, App Launcher, and Quick Settings elements
  - [ ] Font scaling at 2.0x renders all shell text at double size (verified by screenshot comparison)
  - [ ] High-contrast mode changes all shell elements to high-contrast palette
  - [ ] Accessibility settings persist after reboot (read from accessibility.json)
  - [ ] `~/.config/zenos/accessibility.json` is created with default values on first access

### Sub-Phase 7.3: Configuration Reset and Corruption Recovery ⬜

- **Goal**: System provides a configuration reset option and detects/recovers from corrupted config files.
- **Tasks**:
  - Implement config reset: restore system defaults in /etc without affecting /home
  - Implement corruption detection: validate JSON config files on load, fall back to defaults on parse error
  - Notify user when a corrupted config is detected and defaults are applied
- **Quality Gate**:
  - [ ] Config reset restores default shelf.json, theme.json, accessibility.json without deleting /home data
  - [ ] A deliberately corrupted shelf.json (invalid JSON) triggers fallback to defaults with a notification toast
  - [ ] After config reset, all shell elements render with default settings
  - [ ] /home directory contents are unchanged after config reset

### Sub-Phase 7.4: Headless Mode and Kiosk Mode ⬜

- **Goal**: System supports headless operation (no compositor) and kiosk mode (single-app fullscreen).
- **Tasks**:
  - Implement headless boot target: `systemctl isolate zenos-headless.target` stops compositor and shell
  - Implement kiosk mode: read `KioskConfig` from `/etc/zenos/kiosk.json`, launch designated app fullscreen, hide shell, auto-restart on crash
  - In kiosk mode: disable automatic suspend on idle, disable lid-close suspend
  - In kiosk mode: block removable storage mounting unless explicitly allowlisted
- **Quality Gate**:
  - [ ] `systemctl isolate zenos-headless.target` stops the compositor and no GUI is running
  - [ ] In kiosk mode, only the designated application is visible (no Shelf, no App Launcher)
  - [ ] Killing the kiosk app causes automatic restart within 3 seconds
  - [ ] In kiosk mode, inserting a USB drive does not auto-mount (unless allowlisted)
  - [ ] In kiosk mode, closing the laptop lid does not suspend the system

---

## Phase 8: Integration Testing & Stabilization (v0.8.x)

**Goal**: End-to-end testing, performance validation, security audit, and bug fixing.

**Status**: ⬜ Not Started

### Sub-Phase 8.1: D-Bus Integration Tests ⬜

- **Goal**: All D-Bus interfaces are tested for correct method signatures, error handling, and inter-component communication.
- **Tasks**:
  - Write integration tests for each D-Bus interface: Compositor, UpdateManager, PackageManager, PrivacyGuard, ResourceManager
  - Test error paths: invalid arguments return `ZEN_ERR_INVALID_ARGUMENT`, unauthorized calls return `ZEN_ERR_PERMISSION_DENIED`
  - Test signal emission: verify signals are emitted on state changes
- **Quality Gate**:
  - [ ] `meson test -C builddir --suite integration` exits 0 with all D-Bus tests passed
  - [ ] Each D-Bus interface has ≥ 3 test cases (happy path, error path, signal)
  - [ ] All error codes match the `ZenError` enum values

### Sub-Phase 8.2: Boot-to-Desktop End-to-End Test ⬜

- **Goal**: Full boot sequence from power-on to usable desktop is tested and passes all quality gates.
- **Tasks**:
  - Implement end-to-end test via zen-test-cli: boot VM, verify ZEN_BOOT_OK, verify Shelf renders, verify App Launcher opens, verify Quick Settings opens
  - Measure boot time: must be < 15 seconds on QEMU reference hardware
  - Verify all systemd services start without errors
- **Quality Gate**:
  - [ ] `zen-test-cli boot-test` exits 0
  - [ ] Boot time from BIOS to ZEN_BOOT_OK < 15 seconds on QEMU
  - [ ] `systemctl --failed` shows 0 failed units
  - [ ] Screenshot shows Shelf at bottom, desktop background rendered

### Sub-Phase 8.3: Memory Budget Validation ⬜

- **Goal**: Memory budgets are met on both 2 GB and 4 GB reference hardware profiles.
- **Tasks**:
  - Measure total desktop session RSS on 2 GB VM (kiosk mode): must be ≤ 70 MB
  - Measure total desktop session RSS on 4 GB VM (full desktop + Waydroid): must be ≤ 70 MB (desktop) + Waydroid overhead
  - Measure PipeWire + WirePlumber RSS: must be ≤ 12 MB
  - Profile and optimize any components exceeding budget
- **Quality Gate**:
  - [ ] Compositor core RSS ≤ 30 MB (verified by `/proc/<pid>/status` VmRSS)
  - [ ] Shell components RSS ≤ 40 MB
  - [ ] Total desktop session RSS ≤ 70 MB
  - [ ] PipeWire + WirePlumber combined RSS ≤ 12 MB
  - [ ] System is responsive on 2 GB VM (window open/close < 500ms)

### Sub-Phase 8.4: Security and Privacy Audit ⬜

- **Goal**: All security and privacy mechanisms are verified end-to-end.
- **Tasks**:
  - Verify AppArmor: all 6 profiles in enforce mode, no denials during normal operation
  - Verify firewall: default-deny inbound, Privacy Guard outbound blocking active
  - Verify polkit: all privileged operations require authentication
  - Verify OSTree read-only root: direct filesystem modification blocked
  - Verify zero telemetry: capture all outbound network traffic for 10 minutes, verify zero telemetry endpoints contacted
- **Quality Gate**:
  - [ ] `aa-status` shows all profiles enforced, 0 denials in audit log during test
  - [ ] `nft list ruleset` shows active firewall with default-deny inbound
  - [ ] 10-minute tcpdump capture shows 0 connections to known telemetry domains
  - [ ] `touch /usr/test` fails with EROFS
  - [ ] Unauthenticated `busctl call` to privileged methods returns permission denied

### Sub-Phase 8.5: Accessibility Audit ⬜

- **Goal**: Screen reader, keyboard navigation, and high-contrast mode are verified across all shell components.
- **Tasks**:
  - Test screen reader with Shelf, App Launcher, Quick Settings, OOBE
  - Test keyboard-only navigation through all shell components
  - Test high-contrast mode visibility of all UI elements
  - Test font scaling at 0.5x, 1.0x, 2.0x, 3.0x
- **Quality Gate**:
  - [ ] Screen reader announces all interactive elements in Shelf, App Launcher, Quick Settings
  - [ ] All shell components are fully navigable using only keyboard
  - [ ] High-contrast mode: all text has contrast ratio ≥ 7:1 against background
  - [ ] Font scaling at 3.0x does not cause text overflow or layout breakage

### Sub-Phase 8.6: Bug Triage and Fix Cycle ⬜

- **Goal**: All P0 bugs are fixed, all P1 bugs are triaged.
- **Tasks**:
  - Run full test suite, collect all failures
  - Triage bugs by priority (P0 = blocks release, P1 = should fix, P2 = nice to have)
  - Fix all P0 bugs
  - Document remaining P1/P2 bugs in issue tracker
- **Quality Gate**:
  - [ ] `meson test -C builddir` exits 0 with all tests passed
  - [ ] 0 P0 bugs open
  - [ ] All P1 bugs documented with reproduction steps
  - [ ] Full boot-to-desktop test passes 10 consecutive times without failure

---

## Phase 9: Release Candidates (v0.9.x)

**Goal**: Produce release candidate images, run full regression testing, finalize documentation, and validate on reference hardware.

**Status**: ⬜ Not Started

### Sub-Phase 9.1: Image Builder — OSTree Compose and ISO Generation ⬜

- **Goal**: Image builder produces a bootable OSTree-based ISO image from the current codebase and configuration.

> **Note**: `tools/image-builder/build-test-image.sh` (Phase 0.5) produces a development/testing qcow2 image for QEMU-based CI and agent validation. In contrast, `compose-ostree.sh` and `build-iso.sh` (this sub-phase) produce the release OSTree deployment and bootable ISO respectively.

- **Tasks**:
  - Implement `tools/image-builder/compose-ostree.sh` — compose an OSTree commit from the build output, base Debian packages, and data/ configuration files
  - Implement `tools/image-builder/build-iso.sh` — generate a bootable ISO with GRUB, initramfs, and the OSTree deployment
  - Include all data/ artifacts: systemd units, AppArmor profiles, nftables rules, polkit rules, zram config, browser configs, icon theme
  - Include LUKS encryption option in installer flow
- **Quality Gate**:
  - [ ] `tools/image-builder/compose-ostree.sh` exits 0 and produces a valid OSTree commit (verified by `ostree log`)
  - [ ] `tools/image-builder/build-iso.sh` exits 0 and produces an ISO file ≥ 500 MB
  - [ ] ISO boots in QEMU and reaches the OOBE wizard
  - [ ] `ostree admin status` on the booted image shows exactly 1 deployment

### Sub-Phase 9.2: Live Session Environment ⬜

- **Goal**: The ISO boots into a live Wayland session where the user can explore the desktop and launch the installer.
- **Tasks**:
  - Implement live session boot flow: GRUB menu entry "Try Zen OS" boots into a temporary OSTree deployment in RAM (tmpfs overlay)
  - Start compositor and shell in live mode: skip OOBE, use default locale/timezone, auto-login as `zen-live` user
  - Display a desktop shortcut (Cairo-rendered icon on wallpaper) to launch the installer (`src/installer/`)
  - Ensure live session is fully functional: Shelf, App Launcher, Quick Settings, Zen Browser, terminal all work
  - Implement RAM constraint: live session operates within 1 GB tmpfs overlay (changes are discarded on reboot)
  - Add "Install Zen OS" entry to App Launcher that launches the installer
- **Quality Gate**:
  - [ ] ISO boots to GRUB with "Try Zen OS" and "Install Zen OS" menu entries
  - [ ] "Try Zen OS" boots into a live desktop session with Shelf and wallpaper visible
  - [ ] Desktop shortcut and App Launcher both offer a path to launch the installer
  - [ ] Live session is fully functional: apps launch, Quick Settings toggles work, browser opens
  - [ ] Rebooting the live session discards all changes (no persistent state)
  - [ ] Live session operates within 1 GB tmpfs (verified by `df -h /` showing tmpfs)

### Sub-Phase 9.3: Full Regression Test Suite ⬜

- **Goal**: A comprehensive automated test suite covers all critical paths and passes on the RC image.
- **Tasks**:
  - Consolidate all unit tests into `meson test -C builddir --suite unit`
  - Consolidate all integration tests into `meson test -C builddir --suite integration`
  - Implement end-to-end test script: boot ISO in QEMU, complete OOBE, verify Shelf/App Launcher/Quick Settings, launch an app, toggle dark mode, send a notification
  - Implement stress test: 100 consecutive client open/close cycles, 50 D-Bus method calls, 10 update/rollback cycles
- **Quality Gate**:
  - [ ] `meson test -C builddir --suite unit` exits 0 with all tests passed
  - [ ] `meson test -C builddir --suite integration` exits 0 with all tests passed
  - [ ] End-to-end test script exits 0 on the RC image
  - [ ] Stress test completes with 0 ASan errors and 0 LeakSanitizer errors
  - [ ] 0 P0 bugs open after full regression run

### Sub-Phase 9.4: Reference Hardware Validation ⬜

- **Goal**: RC image boots and operates correctly on both QEMU reference and physical Chromebook reference hardware.
- **Tasks**:
  - Boot RC ISO on QEMU reference hardware (2 vCPUs, 2 GB RAM, virtio-gpu)
  - Boot RC ISO on Lenovo IdeaPad Slim 3i Chromebook 14 (Intel N100, 4 GB RAM)
  - Verify all hardware: display, keyboard, touchpad, Wi-Fi, audio, USB
  - Measure boot time, memory usage, and responsiveness on both targets
- **Quality Gate**:
  - [ ] QEMU boot: ZEN_BOOT_OK in < 15 seconds, Shelf renders, all services start
  - [ ] Chromebook boot: display renders at native 1920x1080, touchpad and keyboard functional
  - [ ] Chromebook Wi-Fi connects to a WPA2 network
  - [ ] Chromebook audio plays through speakers (verified by `aplay` test tone)
  - [ ] Memory budget met on both targets: desktop session RSS ≤ 70 MB

### Sub-Phase 9.5: Documentation Finalization ⬜

- **Goal**: All user-facing and developer documentation is complete, accurate, and consistent with the shipped software.
- **Tasks**:
  - Write `docs/USER_GUIDE.md` — end-user guide covering OOBE, Shelf, App Launcher, Quick Settings, browser, Flatpak, updates, privacy settings
  - Write `docs/ADMIN_GUIDE.md` — system administrator guide covering headless mode, kiosk mode, OSTree management, AppArmor, firewall, Waydroid
  - Update `docs/CONTRIBUTING.md` with final build instructions and test procedures
  - Update `docs/CHANGELOG.md` with all changes since last entry
  - Verify all ADRs in `docs/architecture/` are written and accurate
- **Quality Gate**:
  - [ ] `docs/USER_GUIDE.md` exists and covers all 7 major user-facing features (Shelf, App Launcher, Quick Settings, Browser, Flatpak, Updates, Privacy)
  - [ ] `docs/ADMIN_GUIDE.md` exists and covers headless mode, kiosk mode, and OSTree management
  - [ ] `docs/CONTRIBUTING.md` contains accurate build and test instructions (verified by following them on a clean checkout)
  - [ ] All documentation files are written in English

### Sub-Phase 9.6: Release Candidate Tagging ⬜

- **Goal**: RC images are tagged in git, published for testing, and a known-issues list is documented.
- **Tasks**:
  - Tag `v0.9.0-rc1` in git with annotated tag describing the release
  - Build final RC ISO from the tagged commit
  - Document known issues and workarounds in `docs/KNOWN_ISSUES.md`
  - Verify the tagged commit builds and tests cleanly from a fresh clone
- **Quality Gate**:
  - [ ] `git tag -l 'v0.9.0-rc*'` shows at least one RC tag
  - [ ] ISO built from the tagged commit boots and passes the end-to-end test
  - [ ] `docs/KNOWN_ISSUES.md` exists and lists all known P1/P2 bugs with workarounds
  - [ ] Fresh `git clone` + `meson setup builddir` + `meson compile -C builddir` + `meson test -C builddir` exits 0

---

## Phase 10: First Stable Release (v1.0.0)

**Goal**: Ship the first stable release of Zen OS with all features complete, all tests passing, and all documentation finalized.

**Status**: ⬜ Not Started

### Sub-Phase 10.1: Final Bug Fixes ⬜

- **Goal**: All P0 and P1 bugs from RC testing are fixed.
- **Tasks**:
  - Fix all remaining P0 bugs identified during RC testing
  - Fix all P1 bugs that are feasible to resolve before release
  - Run full regression suite after each fix to prevent regressions
  - Update KNOWN_ISSUES.md to remove fixed bugs and document any remaining P2 issues
- **Quality Gate**:
  - [ ] 0 P0 bugs open
  - [ ] 0 P1 bugs open (or explicitly deferred to v1.1 with justification)
  - [ ] Full regression suite passes after all fixes applied
  - [ ] `docs/KNOWN_ISSUES.md` updated with current status

### Sub-Phase 10.2: Release Build and Signing ⬜

- **Goal**: Final release ISO is built from a clean tagged commit, reproducibly, with GPG-signed OSTree commits.
- **Tasks**:
  - Tag `v1.0.0` in git with annotated tag
  - Build release ISO from the tagged commit using `tools/image-builder/`
  - Sign OSTree commit with project GPG key
  - Verify ISO checksum and GPG signature
  - Build from a second clean clone to verify reproducibility
- **Quality Gate**:
  - [ ] `git tag -l 'v1.0.0'` shows the release tag
  - [ ] ISO built from tagged commit boots and passes end-to-end test
  - [ ] `ostree show --gpg-verify` confirms valid GPG signature on the deployment commit
  - [ ] SHA256 checksum of ISO from two independent builds matches (reproducible build — aspirational, not blocking)

### Sub-Phase 10.3: Release Validation ⬜

- **Goal**: Release ISO passes all quality gates on both reference hardware targets with zero critical issues.
- **Tasks**:
  - Boot release ISO on QEMU reference hardware — full end-to-end test
  - Boot release ISO on Chromebook reference hardware — full hardware validation
  - Run 24-hour soak test on QEMU: continuous operation with periodic app launches, D-Bus calls, and update checks
  - Verify zero telemetry during soak test (tcpdump capture)
  - Verify memory usage remains stable over 24 hours (no memory leaks)
- **Quality Gate**:
  - [ ] QEMU end-to-end test passes
  - [ ] Chromebook hardware validation passes (display, input, Wi-Fi, audio, USB)
  - [ ] 24-hour soak test: 0 crashes, 0 ASan errors, 0 LeakSanitizer errors
  - [ ] 24-hour tcpdump: 0 connections to telemetry endpoints
  - [ ] Memory RSS delta over 24 hours ≤ 5 MB (no significant leak)

### Sub-Phase 10.4: Release Publication ⬜

- **Goal**: Release artifacts are published and all documentation reflects the v1.0.0 state.
- **Tasks**:
  - Publish release ISO and SHA256 checksum to GitHub Releases
  - Update `docs/CHANGELOG.md` with v1.0.0 release entry
  - Update `docs/ROADMAP.md` to mark Phase 10 complete
  - Write release announcement summarizing key features and privacy guarantees
  - Verify all links in documentation are valid
- **Quality Gate**:
  - [ ] GitHub Release for v1.0.0 exists with ISO, SHA256, and release notes
  - [ ] `docs/CHANGELOG.md` contains v1.0.0 entry with date and feature summary
  - [ ] All phases in `docs/ROADMAP.md` are marked with their final status
  - [ ] Release announcement is written in English and accurately describes shipped features
