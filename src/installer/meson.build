# Zen OS Installer — disk partitioning, LUKS, OSTree deploy (Cairo + Pango)

cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')

installer_inc = include_directories('include')

installer_src = files(
    'src/installer.c',
)

executable('zen-installer',
    installer_src,
    include_directories: installer_inc,
    dependencies: [cairo, pango, pangocairo, zen_common_dep],
    install: true,
)
