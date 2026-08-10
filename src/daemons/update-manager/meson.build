# Zen OS Update Manager — OSTree atomic updates, delta downloads, rollback

libsystemd = dependency('libsystemd')
ostree = dependency('ostree-1')

update_manager_inc = include_directories('include')

update_manager_src = files(
    'src/update-manager.c',
)

executable('zen-update-manager',
    update_manager_src,
    include_directories: update_manager_inc,
    dependencies: [libsystemd, ostree, zen_common_dep],
    install: true,
)
