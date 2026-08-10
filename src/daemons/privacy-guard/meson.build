# Zen OS Privacy Guard — telemetry blocking, nftables, DNS filtering

libsystemd = dependency('libsystemd')

privacy_guard_inc = include_directories('include')

privacy_guard_src = files(
    'src/privacy-guard.c',
)

executable('zen-privacy-guard',
    privacy_guard_src,
    include_directories: privacy_guard_inc,
    dependencies: [libsystemd, zen_common_dep],
    install: true,
)
