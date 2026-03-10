# Zen OS Compositor — wlroots + SceneFX Wayland compositor

scenefx = dependency('scenefx-0.4', fallback: 'scenefx')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
libsystemd = dependency('libsystemd')
libpam = dependency('libpam', required: false)

# wlroots generates protocol headers (e.g. xdg-shell-protocol.h) into its
# build directory.  These are not exposed via declare_dependency(), so we
# add the path explicitly for any compositor source that includes
# wlr/types/wlr_xdg_shell.h or similar protocol-dependent headers.
wlr_protocol_inc_path = meson.project_build_root() / 'subprojects' / 'wlroots' / 'protocol'

compositor_src = files(
    'src/main.c',
    'src/xdg.c',
    'src/input.c',
)

compositor_inc = include_directories('include')

executable('zen-compositor',
    compositor_src,
    include_directories: compositor_inc,
    dependencies: [scenefx, wayland_server, xkbcommon, cairo, pangocairo, libsystemd, libpam, zen_common_dep],
    c_args: ['-DWLR_USE_UNSTABLE', '-I' + wlr_protocol_inc_path],
    install: true,
)

