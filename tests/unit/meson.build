# Zen OS unit tests — CMocka-based test suite

cmocka = dependency('cmocka', required: true)

test_dbus_errors = executable('test-dbus-errors',
    'test_dbus_errors.c',
    dependencies: [cmocka, zen_common_dep],
    install: false,
)

test('dbus-errors', test_dbus_errors,
    suite: 'unit',
    timeout: 30,
)
