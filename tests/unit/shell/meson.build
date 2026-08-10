# Zen OS shell unit tests

cmocka = dependency('cmocka', required: true)

test_inc = include_directories('../../include')
shell_inc = include_directories('../../../src/shell/include')

# test_shelf — Properties 1, 2 (pure logic, no wlroots needed)
test_shelf = executable('test-shelf',
    'test_shelf.c',
    include_directories: [test_inc],
    dependencies: [cmocka],
    install: false,
)

test('test_shelf', test_shelf,
    suite: 'unit',
    timeout: 60,
)

# test_theme_config — Properties 8, 9
theme_config_src = files('../../../src/shell/src/theme-config.c')

test_theme_config = executable('test-theme-config',
    'test_theme_config.c',
    theme_config_src,
    include_directories: [test_inc, shell_inc],
    dependencies: [cmocka],
    install: false,
)

test('test_theme_config', test_theme_config,
    suite: 'unit',
    timeout: 60,
    env: ['ASAN_OPTIONS=detect_leaks=0'],
)

# test_tray — Properties 23, 26 (pure logic, no wlroots needed)
test_tray = executable('test-tray',
    'test_tray.c',
    include_directories: [test_inc],
    dependencies: [cmocka],
    install: false,
)

test('test_tray', test_tray,
    suite: 'shell',
    timeout: 60,
)
