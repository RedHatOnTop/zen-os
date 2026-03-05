# Zen OS Shell — In-process desktop shell module (Cairo + Pango)
#
# Compiled as a static library linked into the compositor binary.
# Components: Shelf, App Launcher, Quick Settings, Notifications.

cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')

zen_shell_inc = include_directories('include')

zen_shell_src = files(
    'src/shell.c',
    'src/shelf.c',
    'src/app-launcher.c',
    'src/quick-settings.c',
    'src/notifications.c',
)

zen_shell_lib = static_library('zen-shell',
    zen_shell_src,
    include_directories: zen_shell_inc,
    dependencies: [cairo, pango, pangocairo, zen_common_dep],
    install: false,
)

zen_shell_dep = declare_dependency(
    link_with: zen_shell_lib,
    include_directories: zen_shell_inc,
)
