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

**Goal**: Establish project structure, documentation, build system, interface contracts, and CI pipeline.

**Status**: 🟡 In Progress (partial — build system and core scaffolding done)

| Task | Description | Status |
|------|-------------|--------|
| 0.1 | Root project files (meson.build, meson_options.txt, AGENTS.md) | ✅ Complete |
| 0.2 | Documentation scaffolding (ROADMAP, CHANGELOG) | ✅ Complete |
| 0.3 | Source directory scaffolding (src/common, src/compositor) | ✅ Complete |
| 0.4 | Subproject wraps (SceneFX, wlroots, wayland, pixman) | ✅ Complete |
| 0.5 | D-Bus interface XML contracts | ⬜ Not Started |
| 0.6 | systemd unit file templates | ⬜ Not Started |
| 0.7 | AppArmor, nftables, polkit, zram config templates | ⬜ Not Started |
| 0.8 | Icon theme scaffolding | ⬜ Not Started |
| 0.9 | CI pipeline (GitHub Actions) | ⬜ Not Started |
| 0.10 | Scaffolding verification checkpoint | ⬜ Not Started |

**Exit Criteria**:
- `meson setup builddir` succeeds ✅
- All D-Bus XML files are well-formed
- All systemd units pass `systemd-analyze verify`
- CI pipeline runs (build + lint + test stubs)

---

## Phase 1: Foundation — Boot, Compositor Core, Session (v0.1.x)

**Goal**: Boot into a minimal Wayland session with window management and user login.

**Status**: 🟡 In Progress

| Task | Description | Dependencies | Priority | Status |
|------|-------------|--------------|----------|--------|
| 1.1 | **Minimal compositor — empty frame (SceneFX + headless QEMU)** | Phase 0 | P0 | ✅ Complete |
| 1.2 | OSTree deployment health check and automatic rollback | Phase 0 | P0 | ⬜ |
| 1.3 | Boot sequence: systemd → OSTree mount → zram → cgroups → logind | Phase 0 | P0 | ⬜ |
| 1.4 | xdg_shell window lifecycle: map, unmap, configure, close | 1.1 | P0 | ⬜ |
| 1.5 | Input routing: keyboard, pointer, touch → focused surface | 1.1 | P0 | ⬜ |
| 1.6 | Multi-monitor: wlr_output_layout, per-display resolution/scaling | 1.1 | P1 | ⬜ |
| 1.7 | XWayland bridge for legacy X11 apps | 1.1 | P1 | ⬜ |
| 1.8 | Crash isolation: client disconnect → surface cleanup | 1.4 | P0 | ⬜ |
| 1.9 | Compositor D-Bus interface (org.zenos.Compositor) | 1.1 | P0 | ⬜ |
| 1.10 | Session Manager: PAM auth → logind session → compositor start | 1.1 | P0 | ⬜ |
| 1.11 | Multi-user support: isolated home directories | 1.10 | P1 | ⬜ |
| 1.12 | Cairo + Pango shell rendering integration: texture buffer pipeline | 1.1 | P0 | ⬜ |
| 1.13 | Memory budget validation: compositor ≤ 30 MB, shell ≤ 40 MB, total ≤ 70 MB RSS idle | 1.1, 1.12 | P0 | ⬜ |

**Milestone Deliverable**: Boot to login → authenticate → see a Wayland desktop with basic window management.

**Exit Criteria**:
- System boots from OSTree deployment in < 15s on VM, < 30s on Chromebook reference hardware
- Compositor manages windows, routes input, handles crashes
- SceneFX blur, shadows, and rounded corners functional
- Cairo + Pango renders shell widget content into compositor scene graph
- Compositor core idle RSS ≤ 30 MB
- User can log in via PAM, session starts via logind
- Automatic rollback works on failed deployment

---

## Phase 2: Desktop Shell (v0.2.x)

**Goal**: ChromeOS-inspired desktop shell with Zen Browser visual identity.

**Estimated Duration**: 4–6 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 2.1 | Shelf: persistent bottom bar rendered in-process via Cairo + Pango | Phase 1 | P0 |
| 2.2 | Shelf: pinned apps, running indicators, system tray (battery, BT, network, volume) | 2.1 | P0 |
| 2.3 | Shelf: drag-to-pin, right-click context menu | 2.1 | P1 |
| 2.4 | Shelf: config persistence (~/.config/zenos/shelf.json) | 2.1 | P0 |
| 2.5 | App Launcher: fullscreen overlay grid with search, rendered via Cairo + Pango | Phase 1 | P0 |
| 2.6 | App Launcher: scan .desktop files, Flatpak exports, PWAs | 2.5 | P0 |
| 2.7 | App Launcher: keyboard navigation (accessibility) | 2.5 | P0 |
| 2.8 | Quick Settings: popup panel with Wi-Fi, BT, volume, brightness, DND | Phase 1 | P0 |
| 2.9 | Quick Settings: D-Bus integration with NetworkManager, BlueZ, PipeWire, UPower | 2.8 | P0 |
| 2.10 | Notification Manager: org.freedesktop.Notifications, floating toasts via Cairo | Phase 1 | P0 |
| 2.11 | Visual styling via SceneFX: Gaussian blur, drop shadows, rounded corners (≥ 8px) | 2.1, 2.5, 2.8 | P0 |
| 2.12 | Dark mode / light mode: system-wide toggle, persistence | 2.11 | P0 |
| 2.13 | Default icon theme: Zen Browser visual identity | Phase 1 | P1 |
| 2.14 | System tray: battery indicator (UPower D-Bus), BT status (BlueZ D-Bus) | 2.2 | P0 |
| 2.15 | System tray: removable storage indicator (udisks2 D-Bus) | 2.2 | P1 |

**Milestone Deliverable**: Full desktop shell with Shelf, App Launcher, Quick Settings, notifications, and theming.

**Exit Criteria**:
- Shelf renders with pinned apps, running indicators, system tray
- App Launcher shows all installed apps with working search
- Quick Settings toggles control real system settings
- Notifications display as floating toasts
- Dark/light mode works system-wide
- All UI elements use rounded corners, blur, drop shadows

---

## Phase 3: Application Ecosystem (v0.3.x)

**Goal**: Zen Browser integration, Flatpak support, OSTree package layering, PWAs.

**Estimated Duration**: 4–6 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 3.1 | Zen Browser build: telemetry removal, privacy hardening | Phase 1 | P0 |
| 3.2 | Zen Browser auto-launch on session start | 3.1, Phase 1 | P0 |
| 3.3 | Zen Browser crash recovery: restart dialog + session restore | 3.1 | P0 |
| 3.4 | PWA registration: .desktop file generation, App Launcher integration | 3.1, Phase 2 | P0 |
| 3.5 | Flatpak install/remove via Package Manager daemon | Phase 1 | P0 |
| 3.6 | Flatpak sandbox enforcement + permission prompting | 3.5 | P0 |
| 3.7 | OSTree native package layering (LayerPackage/UnlayerPackage) | Phase 1 | P0 |
| 3.8 | OSTree layering conflict detection | 3.7 | P0 |
| 3.9 | Block direct APT modification of read-only root | 3.7 | P0 |
| 3.10 | Unified package listing and search (Flatpak + layered) | 3.5, 3.7 | P0 |
| 3.11 | Package Manager D-Bus interface (org.zenos.PackageManager) | 3.5, 3.7 | P0 |
| 3.12 | Terminal emulator bundled as native app | Phase 1 | P1 |

**Milestone Deliverable**: Users can browse the web, install PWAs, install Flatpak apps, and layer native Debian packages.

**Exit Criteria**:
- Zen Browser launches with zero telemetry endpoints
- PWAs appear in App Launcher
- Flatpak apps install sandboxed with permission prompts
- Native packages layer onto OSTree without breaking immutability
- Direct APT blocked on read-only root

---

## Phase 4: System Services (v0.4.x)

**Goal**: Privacy Guard, Resource Manager, Update Manager, Network Manager.

**Estimated Duration**: 5–7 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 4.1 | Privacy Guard: nftables telemetry blocking | Phase 1 | P0 |
| 4.2 | Privacy Guard: DNS-level filtering | 4.1 | P0 |
| 4.3 | Privacy Guard: audit log + D-Bus interface | 4.1 | P0 |
| 4.4 | Update Manager: OSTree delta downloads | Phase 1 | P0 |
| 4.5 | Update Manager: GPG signature verification | 4.4 | P0 |
| 4.6 | Update Manager: atomic deployment + rollback | 4.4 | P0 |
| 4.7 | Update Manager: security update prioritization + notification | 4.4, Phase 2 | P0 |
| 4.8 | Update Manager: /etc and /home preservation (3-way merge) | 4.4 | P0 |
| 4.9 | Resource Manager: zram configuration at boot | Phase 1 | P0 |
| 4.10 | Resource Manager: PSI memory pressure monitoring | 4.9 | P0 |
| 4.11 | Resource Manager: cgroups v2 freeze/thaw for background processes | 4.10 | P0 |
| 4.12 | Resource Manager: RAM detection + Waydroid gate (< 4 GB → disabled) | 4.10 | P0 |
| 4.13 | Network Manager: Wi-Fi/Ethernet, auto-connect, credential storage | Phase 1 | P0 |
| 4.14 | Network Manager: DNS-over-HTTPS via systemd-resolved | 4.13 | P0 |
| 4.15 | Network Manager: VPN support (WireGuard, OpenVPN) | 4.13 | P1 |
| 4.16 | Network Manager: connection loss notification + reconnection | 4.13, Phase 2 | P0 |

**Milestone Deliverable**: All system daemons operational — privacy enforced, updates working, resources managed, networking functional.

**Exit Criteria**:
- Privacy Guard blocks all known telemetry domains
- Updates download deltas, verify signatures, deploy atomically
- Rollback works on failed update
- zram active, PSI monitoring triggers freeze/thaw
- Wi-Fi connects, DNS-over-HTTPS active, VPN functional

---

## Phase 5: Android Support (v0.5.x)

**Goal**: Waydroid integration for Android app compatibility.

**Estimated Duration**: 3–4 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 5.1 | Waydroid initialization with Android system image | Phase 4 (Resource Manager) | P0 |
| 5.2 | Waydroid windows as managed compositor surfaces | Phase 1 | P0 |
| 5.3 | Clipboard sharing between Android and native apps | 5.1 | P1 |
| 5.4 | cgroups v2 memory/CPU limits on Waydroid container | 5.1 | P0 |
| 5.5 | Freeze Waydroid on focus-away, thaw on focus-back (< 2s) | 5.4 | P0 |
| 5.6 | 4 GB RAM gate: disable Waydroid on < 4 GB systems | Phase 4 | P0 |
| 5.7 | Waydroid disable/uninstall without affecting core system | 5.1 | P0 |
| 5.8 | Error handling: descriptive errors + troubleshooting guidance | 5.1 | P0 |

**Milestone Deliverable**: Android apps run in managed windows with resource isolation.

**Exit Criteria**:
- Waydroid starts on ≥ 4 GB systems, blocked on < 4 GB
- Android apps display in compositor windows
- Container freezes on focus-away, thaws in < 2s
- Waydroid removable without side effects

---

## Phase 6: Security Hardening (v0.6.x)

**Goal**: AppArmor, firewall, encryption, polkit — defense in depth.

**Estimated Duration**: 2–3 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 6.1 | AppArmor profiles for all bundled services | Phase 4 | P0 |
| 6.2 | nftables firewall: default-deny inbound | Phase 4 | P0 |
| 6.3 | LUKS full-disk encryption option in OOBE | Phase 1 (OOBE) | P1 |
| 6.4 | polkit rules for privilege escalation | Phase 3, Phase 4 | P0 |
| 6.5 | Verify OSTree read-only root enforcement end-to-end | Phase 3 | P0 |

**Milestone Deliverable**: All services confined by AppArmor, firewall active, privilege escalation gated.

---

## Phase 7: Polish & Accessibility (v0.7.x)

**Goal**: OOBE wizard, accessibility features, configuration persistence, headless mode.

**Estimated Duration**: 3–4 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 7.1 | OOBE wizard: locale, timezone, keyboard, user, network | Phase 1 | P0 |
| 7.2 | OOBE: keyboard-only navigation | 7.1 | P0 |
| 7.3 | OOBE: power-loss recovery (restart on next boot) | 7.1 | P0 |
| 7.4 | AT-SPI2 screen reader integration | Phase 2 | P0 |
| 7.5 | System-wide font scaling + high-contrast mode | Phase 2 | P0 |
| 7.6 | Accessibility config persistence | 7.4, 7.5 | P0 |
| 7.7 | Configuration reset (restore defaults, preserve /home) | Phase 4 | P1 |
| 7.8 | Corrupted config detection + fallback | 7.7 | P1 |
| 7.9 | Headless boot target (no compositor) | Phase 1 | P2 |
| 7.10 | Kiosk Mode: single-app fullscreen, auto-restart, shell disabled | Phase 2, Phase 4 | P1 |
| 7.11 | Mozilla account sync support in Zen Browser | Phase 3 | P2 |

**Milestone Deliverable**: First-boot experience polished, accessibility functional, kiosk mode working.

---

## Phase 8: Integration Testing & Stabilization (v0.8.x)

**Goal**: End-to-end testing, performance validation, bug fixing.

**Estimated Duration**: 4–6 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 8.1 | D-Bus integration tests: all component pairs | All phases | P0 |
| 8.2 | Boot-to-desktop end-to-end test | All phases | P0 |
| 8.3 | Update + rollback end-to-end test | Phase 4 | P0 |
| 8.4 | Memory budget validation on 2 GB reference hardware | All phases | P0 |
| 8.5 | Memory budget validation on 4 GB reference hardware (with Waydroid) | Phase 5 | P0 |
| 8.6 | Privacy audit: verify zero outbound telemetry | Phase 4 | P0 |
| 8.7 | Security audit: AppArmor, firewall, polkit, read-only root | Phase 6 | P0 |
| 8.8 | Accessibility audit: screen reader, keyboard nav, high contrast | Phase 7 | P0 |
| 8.9 | Performance profiling and optimization | All phases | P0 |
| 8.10 | Bug triage and fix cycle | All phases | P0 |

**Milestone Deliverable**: All tests passing, performance targets met, no P0 bugs open.

---

## Phase 9: Release Candidates (v0.9.x)

**Goal**: Final polish, documentation, ISO image generation.

**Estimated Duration**: 2–3 weeks

| Task | Description | Dependencies | Priority |
|------|-------------|--------------|----------|
| 9.1 | ISO image build pipeline | All phases | P0 |
| 9.2 | Installation media (USB boot) | 9.1 | P0 |
| 9.3 | Final documentation review | All phases | P0 |
| 9.4 | Release notes | All phases | P0 |
| 9.5 | Community testing / beta feedback | 9.1 | P0 |

---

## Phase 10: First Stable Release (v1.0.0)

**Goal**: Ship it.

| Task | Description |
|------|-------------|
| 10.1 | Final bug fixes from RC feedback |
| 10.2 | Tag v1.0.0 |
| 10.3 | Publish ISO images |
| 10.4 | Announce release |

---

## Timeline Summary

| Phase | Version | Duration | Cumulative |
|-------|---------|----------|------------|
| 0. Planning & Setup | — | 1–2 weeks | 1–2 weeks |
| 1. Foundation | 0.1.x | 6–8 weeks | 7–10 weeks |
| 2. Desktop Shell | 0.2.x | 4–6 weeks | 11–16 weeks |
| 3. Application Ecosystem | 0.3.x | 4–6 weeks | 15–22 weeks |
| 4. System Services | 0.4.x | 5–7 weeks | 20–29 weeks |
| 5. Android Support | 0.5.x | 3–4 weeks | 23–33 weeks |
| 6. Security Hardening | 0.6.x | 2–3 weeks | 25–36 weeks |
| 7. Polish & Accessibility | 0.7.x | 3–4 weeks | 28–40 weeks |
| 8. Integration & Stabilization | 0.8.x | 4–6 weeks | 32–46 weeks |
| 9. Release Candidates | 0.9.x | 2–3 weeks | 34–49 weeks |
| 10. Stable Release | 1.0.0 | 1–2 weeks | 35–51 weeks |

**Estimated total: ~9–12 months to v1.0.0**
