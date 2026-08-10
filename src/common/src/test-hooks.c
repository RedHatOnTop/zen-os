/*
 * Zen OS — Test Hook Utilities Implementation
 *
 * Provides atomic JSON file writes and small JSON string escaping helpers for
 * compositor and shell test-only state snapshots.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "zen/test-hooks.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int zen_test_hooks_ensure_dir(void)
{
    struct stat st;

    if (stat(ZEN_TEST_HOOKS_DIR, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        errno = ENOTDIR;
        return -1;
    }

    if (mkdir(ZEN_TEST_HOOKS_DIR, 0755) == 0 || errno == EEXIST) {
        return 0;
    }

    return -1;
}

int zen_test_hooks_write_atomic(const char *path, const char *content)
{
    int ret = -1;
    int fd = -1;
    FILE *stream = NULL;
    char *tmp_path = NULL;
    const char *slash = NULL;
    size_t dir_len = 0;

    if (!path || !content) {
        errno = EINVAL;
        goto cleanup;
    }

    if (zen_test_hooks_ensure_dir() != 0) {
        goto cleanup;
    }

    slash = strrchr(path, '/');
    if (!slash) {
        errno = EINVAL;
        goto cleanup;
    }

    dir_len = (size_t)(slash - path);
    tmp_path = calloc(1, dir_len + sizeof("/.tmp-XXXXXX"));
    if (!tmp_path) {
        goto cleanup;
    }

    memcpy(tmp_path, path, dir_len);
    memcpy(tmp_path + dir_len, "/.tmp-XXXXXX", sizeof("/.tmp-XXXXXX"));

    fd = mkstemp(tmp_path);
    if (fd < 0) {
        goto cleanup;
    }

    stream = fdopen(fd, "w");
    if (!stream) {
        goto cleanup;
    }
    fd = -1;

    if (fputs(content, stream) == EOF) {
        goto cleanup;
    }
    if (fflush(stream) != 0) {
        goto cleanup;
    }
    if (fsync(fileno(stream)) != 0) {
        goto cleanup;
    }
    if (fclose(stream) != 0) {
        stream = NULL;
        goto cleanup;
    }
    stream = NULL;

    if (rename(tmp_path, path) != 0) {
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (stream) {
        fclose(stream);
    } else if (fd >= 0) {
        close(fd);
    }

    if (ret != 0 && tmp_path) {
        unlink(tmp_path);
    }

    free(tmp_path);
    return ret;
}

int zen_test_hooks_remove_file(const char *path)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if (unlink(path) == 0 || errno == ENOENT) {
        return 0;
    }

    return -1;
}

void zen_test_hooks_write_json_string(FILE *stream, const char *value)
{
    const unsigned char *p = NULL;

    if (!stream) {
        return;
    }

    if (!value) {
        value = "";
    }

    fputc('"', stream);
    for (p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
        case '\\':
            fputs("\\\\", stream);
            break;
        case '"':
            fputs("\\\"", stream);
            break;
        case '\n':
            fputs("\\n", stream);
            break;
        case '\r':
            fputs("\\r", stream);
            break;
        case '\t':
            fputs("\\t", stream);
            break;
        default:
            if (*p < 0x20) {
                fprintf(stream, "\\u%04x", (unsigned int)*p);
            } else {
                fputc((int)*p, stream);
            }
            break;
        }
    }
    fputc('"', stream);
}
