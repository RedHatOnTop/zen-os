# Zen OS OOBE — First-boot wizard (Cairo + Pango)

cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')

oobe_inc = include_directories('include')

oobe_src = files(
    'src/oobe.c',
)

executable('zen-oobe',
    oobe_src,
    include_directories: oobe_inc,
    dependencies: [cairo, pango, pangocairo, zen_common_dep],
    install: true,
)
