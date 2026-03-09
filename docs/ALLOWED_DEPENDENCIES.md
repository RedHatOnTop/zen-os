# Allowed External Dependencies

This file is the single source of truth for all external libraries sanctioned in the Zen OS project. Any dependency not listed here requires explicit user approval before use. See `AGENTS.md` Section 0 (Extreme Minimalist) for the rationale.

## Core Libraries

| pkg-config Name | Library | Purpose | Used By |
|---|---|---|---|
| `scenefx` | SceneFX | Compositor rendering effects (blur, shadows, rounded corners) | `src/compositor/` |
| `wlroots` | wlroots | Wayland compositor library (backend, scene graph, protocols) | `src/compositor/` |
| `wayland-server` | libwayland-server | Wayland protocol server implementation | `src/compositor/` |
| `wayland-protocols` | wayland-protocols | Wayland protocol XML definitions (xdg-shell, layer-shell, etc.) | `src/compositor/` |
| `xkbcommon` | libxkbcommon | Keyboard keymap handling and key event processing | `src/compositor/` |
| `cairo` | Cairo | 2D vector graphics rendering for shell UI elements | `src/compositor/`, `src/shell/`, `src/oobe/`, `src/installer/` |
| `pangocairo` | Pango (Cairo backend) | Text layout and font rendering for shell UI elements | `src/compositor/`, `src/shell/`, `src/oobe/`, `src/installer/` |
| `libsystemd` | libsystemd (sd-bus) | D-Bus IPC for all daemon and compositor communication | All components |
| `libostree-1` | libostree | Atomic OS updates, deployment management, rollback | `src/daemons/update-manager/`, `src/daemons/package-manager/`, `src/installer/` |
| `libflatpak` | libflatpak | Flatpak application install, remove, and sandbox management | `src/daemons/package-manager/` |

## Authentication

| pkg-config Name | Library | Purpose | Used By |
|---|---|---|---|
| `libpam` | Linux-PAM | User authentication for login and screen unlock | `src/session/`, `src/compositor/` (lock screen) |

## Optional (compile-time feature flags)

| pkg-config Name | Library | Purpose | Meson Option |
|---|---|---|---|
| `xwayland` | XWayland | X11 application compatibility layer | `enable_xwayland` |

## Test-Only Dependencies

| pkg-config Name | Library | Purpose | Used By |
|---|---|---|---|
| `cmocka` | CMocka | Unit testing framework | `tests/unit/` |

## Permanently Banned

The following toolkits and libraries are permanently banned from this project:

- **GTK** — heavyweight, pulls in GLib/GObject/GIO dependency tree
- **Qt** — heavyweight C++ toolkit, incompatible with project's C17 requirement
- **Skia** — C++ rendering engine, unnecessary given Cairo + SceneFX
- **GLib** (standalone) — not needed when using sd-bus instead of GDBus
- **Electron** — web-based UI framework, antithetical to minimalism

## Adding a New Dependency

1. Open an issue or discuss with the project maintainer
2. Justify why the task cannot be accomplished with existing dependencies or pure C17
3. Verify the library is available as a Meson subproject or system package on Debian
4. Add the entry to the appropriate table above
5. Update any affected `meson.build` files
