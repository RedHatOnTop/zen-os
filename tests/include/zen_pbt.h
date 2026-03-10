/*
 * Zen OS — Property-Based Test Generator Harness
 *
 * Minimal random input generators for PBT tests using CMocka.
 * Each generator uses a seeded PRNG for reproducibility.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_PBT_H
#define ZEN_PBT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Number of iterations per property test. */
#define ZEN_PBT_ITERATIONS 100

/* Seed the PRNG. Call once at test startup. */
static inline void zen_pbt_seed(void) {
    unsigned int seed = (unsigned int)time(NULL);
    srand(seed);
}

/* Random integer in [min, max] (inclusive). */
static inline int zen_pbt_rand_int(int min, int max) {
    if (min >= max) {
        return min;
    }
    return min + (rand() % (max - min + 1));
}

/* Random boolean. */
static inline bool zen_pbt_rand_bool(void) {
    return (rand() % 2) == 0;
}

/* Random dimensions (both > 0). */
static inline void zen_pbt_rand_dimensions(int max_w, int max_h,
                                           int *out_w, int *out_h) {
    *out_w = zen_pbt_rand_int(1, max_w > 0 ? max_w : 3840);
    *out_h = zen_pbt_rand_int(1, max_h > 0 ? max_h : 2160);
}

/*
 * Random ASCII string of length [min_len, max_len].
 * Caller must free() the returned pointer.
 * Returns NULL on allocation failure.
 */
static inline char *zen_pbt_rand_string(int min_len, int max_len) {
    int len = zen_pbt_rand_int(min_len, max_len);
    char *s = calloc(1, (size_t)len + 1);
    if (!s) {
        return NULL;
    }
    for (int i = 0; i < len; i++) {
        /* Printable ASCII range 0x20–0x7E */
        s[i] = (char)zen_pbt_rand_int(0x20, 0x7E);
    }
    s[len] = '\0';
    return s;
}

/* Random WLR_MODIFIER bitmask (subset of bits 0–5). */
static inline uint32_t zen_pbt_rand_modifiers(void) {
    return (uint32_t)(rand() & 0x3F);
}

/* Random xkb_keysym_t in a reasonable range. */
static inline uint32_t zen_pbt_rand_keysym(void) {
    /* XKB keysyms: Latin-1 range 0x0020–0x007E is safe for testing. */
    return (uint32_t)zen_pbt_rand_int(0x0020, 0x007E);
}

#endif /* ZEN_PBT_H */
