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
