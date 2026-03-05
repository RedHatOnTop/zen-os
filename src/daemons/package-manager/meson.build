# Zen OS Package Manager — Flatpak + OSTree layering, unified interface

libsystemd = dependency('libsystemd')
ostree = dependency('ostree-1')
flatpak = dependency('flatpak')

package_manager_inc = include_directories('include')

package_manager_src = files(
    'src/package-manager.c',
)

executable('zen-package-manager',
    package_manager_src,
    include_directories: package_manager_inc,
    dependencies: [libsystemd, ostree, flatpak, zen_common_dep],
    install: true,
)
