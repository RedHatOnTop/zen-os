# Zen OS Resource Manager — cgroups v2, PSI, zram, container lifecycle

libsystemd = dependency('libsystemd')

resource_manager_inc = include_directories('include')

resource_manager_src = files(
    'src/resource-manager.c',
)

executable('zen-resource-manager',
    resource_manager_src,
    include_directories: resource_manager_inc,
    dependencies: [libsystemd, zen_common_dep],
    install: true,
)
