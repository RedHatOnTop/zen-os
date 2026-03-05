# Zen OS Session Manager — systemd-logind, PAM, session lifecycle

libsystemd = dependency('libsystemd')

session_inc = include_directories('include')

session_src = files(
    'src/session.c',
)

executable('zen-session',
    session_src,
    include_directories: session_inc,
    dependencies: [libsystemd, zen_common_dep],
    install: true,
)
