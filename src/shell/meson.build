# Zen OS Shell — In-process desktop shell module (Cairo + Pango)
#
# Compiled as a static library linked into the compositor binary.
# Components: Shelf, App Launcher, Quick Settings, Notifications.

cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
libsystemd = dependency('libsystemd')
scenefx = dependency('scenefx-0.4', fallback: 'scenefx')
wayland_server = dependency('wayland-server')

# Include the compositor headers so shelf.c can use ZenCairoBuffer,
# ZenCompositor, and wlroots types.
compositor_inc = include_directories('../compositor/include')

zen_shell_inc = include_directories('include')

zen_shell_src = files(
    'src/shell.c',
    'src/shell-testctl.c',
    'src/shelf.c',
    'src/shelf-config.c',
    'src/desktop-entry.c',
    'src/app-launcher.c',
    'src/quick-settings.c',
    'src/notifications.c',
    'src/theme-config.c',
)

zen_shell_c_args = ['-DWLR_USE_UNSTABLE']
if get_option('enable_tests')
    zen_shell_c_args += ['-DZEN_ENABLE_TEST_HOOKS']
endif

zen_shell_lib = static_library('zen-shell',
    zen_shell_src,
    include_directories: [zen_shell_inc, compositor_inc],
    dependencies: [cairo, pango, pangocairo, libsystemd, scenefx, wayland_server, zen_common_dep],
    c_args: zen_shell_c_args,
    install: false,
)

zen_shell_dep = declare_dependency(
    link_with: zen_shell_lib,
    include_directories: zen_shell_inc,
)
