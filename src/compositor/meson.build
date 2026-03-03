# Zen OS Compositor — wlroots + SceneFX Wayland compositor

scenefx = dependency('scenefx-0.4', fallback: 'scenefx')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')

compositor_src = files(
    'src/main.c',
)

compositor_inc = include_directories('include')

executable('zen-compositor',
    compositor_src,
    include_directories: compositor_inc,
    dependencies: [scenefx, wayland_server, xkbcommon, zen_common_dep],
    c_args: ['-DWLR_USE_UNSTABLE'],
    install: true,
)

