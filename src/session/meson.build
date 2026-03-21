# Zen OS Session Manager — systemd-logind, PAM, session lifecycle

libsystemd = dependency('libsystemd')
libpam = dependency('libpam', required: false)

pam_args = libpam.found() ? ['-DHAVE_PAM'] : []

session_inc = include_directories('include')

session_src = files(
    'src/session.c',
)

session_deps = [libsystemd, zen_common_dep]
if libpam.found()
    session_deps += [libpam]
endif

executable('zen-session',
    session_src,
    include_directories: session_inc,
    dependencies: session_deps,
    c_args: pam_args,
    install: true,
)
