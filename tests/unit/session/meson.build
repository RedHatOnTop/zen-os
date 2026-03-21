test_session = executable('test-session',
    'test_session.c',
    include_directories: test_inc,
    dependencies: [cmocka],
    install: false,
)

test('test_session', test_session,
    suite: 'unit',
    timeout: 60,
)
