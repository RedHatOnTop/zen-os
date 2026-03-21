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
# Requires a running compositor; skipped on host without QEMU.
test('test_crash_isolation',
    find_program('test_crash_isolation.sh', dirs: [meson.current_source_dir()]),
    suite: 'integration',
    timeout: 60,
    env: ['ASAN_OPTIONS=detect_leaks=0'],
)
