# Zen OS integration tests — lifecycle and ASan validation

# test_asan_lifecycle.sh: Property 26 — ASan/LeakSanitizer clean lifecycle.
# Runs a host-side smoke check (binary exists, no missing libs).
# Full runtime test requires QEMU VM (zen-test gate run).
test('test_asan_lifecycle',
    find_program('test_asan_lifecycle.sh', dirs: [meson.current_source_dir()]),
    suite: 'integration',
    timeout: 30,
    env: ['ASAN_OPTIONS=detect_leaks=0'],
)

# test_crash_isolation.sh: Property 11 — client crash does not affect others.
# Requires a bootable qcow2 VM image. Reads ZEN_OS_IMAGE env var (falls back
# to .zen-test-vms/test-image.qcow2 in the repo root).
test('test_crash_isolation',
    find_program('test_crash_isolation.sh', dirs: [meson.current_source_dir()]),
    suite: 'integration',
    timeout: 360,
    env: [
        'ASAN_OPTIONS=detect_leaks=0',
        'ZEN_OS_IMAGE=' + meson.project_source_root() / '.zen-test-vms/test-image.qcow2',
    ],
)
