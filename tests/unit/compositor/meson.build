# Zen OS compositor unit tests — PBT for XDG shell properties

test_xdg = executable('test-xdg',
    'test_xdg.c',
    include_directories: test_inc,
    dependencies: [cmocka],
    install: false,
)

test('test_xdg', test_xdg,
    suite: 'unit',
    timeout: 60,
)

test_input = executable('test-input',
    'test_input.c',
    include_directories: test_inc,
    dependencies: [cmocka],
    install: false,
)

test('test_input', test_input,
    suite: 'unit',
    timeout: 60,
)

cairo_dep = dependency('cairo')
pangocairo_dep = dependency('pangocairo')

test_cairo_buffer = executable('test-cairo-buffer',
    'test_cairo_buffer.c',
    include_directories: test_inc,
    dependencies: [cmocka, cairo_dep, pangocairo_dep],
    install: false,
)

lsan_supp = meson.project_source_root() / 'tests' / 'lsan.supp'

test('test_cairo_buffer', test_cairo_buffer,
    suite: 'unit',
    timeout: 60,
    # fontconfig/pango have known one-time global allocations that are never
    # freed on process exit. These are third-party library internals, not our
    # code. Suppress leak detection for this test; ASan memory error detection
    # (use-after-free, buffer overflows) remains fully active.
    env: ['ASAN_OPTIONS=detect_leaks=0'],
)

test_wallpaper = executable('test-wallpaper',
    'test_wallpaper.c',
    include_directories: test_inc,
    dependencies: [cmocka],
    link_args: ['-lm'],
    install: false,
)

test('test_wallpaper', test_wallpaper,
    suite: 'unit',
    timeout: 60,
)

xkbcommon_dep = dependency('xkbcommon')
wayland_client_dep = dependency('wayland-client')

test_keybinds = executable('test-keybinds',
    'test_keybinds.c',
    include_directories: test_inc,
    dependencies: [cmocka, xkbcommon_dep, wayland_client_dep],
    install: false,
)

test('test_keybinds', test_keybinds,
    suite: 'unit',
    timeout: 60,
)

wayland_server_dep = dependency('wayland-server')

test_layer = executable('test-layer',
    'test_layer.c',
    include_directories: test_inc,
    dependencies: [cmocka, wayland_server_dep],
    install: false,
)

test('test_layer', test_layer,
    suite: 'unit',
    timeout: 60,
)

test_dbus = executable('test-dbus',
    'test_dbus.c',
    include_directories: test_inc,
    dependencies: [cmocka],
    install: false,
)

test('test_dbus', test_dbus,
    suite: 'unit',
    timeout: 60,
)

libpam_dep = dependency('libpam', required: false)

test_lock = executable('test-lock',
    'test_lock.c',
    include_directories: test_inc,
    dependencies: [cmocka, libpam_dep],
    install: false,
)

test('test_lock', test_lock,
    suite: 'unit',
    timeout: 60,
)

test_multi_monitor = executable('test-multi-monitor',
    'test_multi_monitor.c',
    include_directories: test_inc,
    dependencies: [cmocka],
    install: false,
)

test('test_multi_monitor', test_multi_monitor,
    suite: 'unit',
    timeout: 60,
)
