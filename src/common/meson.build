# Zen OS common library — shared data models, D-Bus errors, utilities

zen_common_inc = include_directories('include')

zen_common_src = files(
    'src/dbus-errors.c',
)

zen_common_lib = static_library('zen-common',
    zen_common_src,
    include_directories: zen_common_inc,
    install: false,
)

zen_common_dep = declare_dependency(
    link_with: zen_common_lib,
    include_directories: zen_common_inc,
)
