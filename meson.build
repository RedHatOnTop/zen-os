project('zen-os', 'c',
    version: '0.1.0',
    license: 'GPL-3.0-or-later',
    meson_version: '>=1.3',
    default_options: [
        'c_std=c17',
        'warning_level=3',
        'b_sanitize=address,undefined',
    ],
)

# enable_asan: adds explicit -fsanitize=address,leak flags on top of the
# default b_sanitize=address,undefined.  Use with buildtype=debug.
if get_option('enable_asan')
    add_project_arguments('-fsanitize=address,leak', language: 'c')
    add_project_link_arguments('-fsanitize=address,leak', language: 'c')
    message('ASan + LeakSanitizer enabled via enable_asan option')
endif

subdir('src/common')
subdir('src/shell')
subdir('src/daemons/resource-manager')
subdir('src/daemons/privacy-guard')
subdir('src/daemons/update-manager')
subdir('src/daemons/package-manager')
subdir('src/session')
subdir('src/oobe')
subdir('src/installer')
subdir('src/compositor')

if get_option('enable_tests')
    subdir('tests/unit')
    subdir('tests/integration')
endif
