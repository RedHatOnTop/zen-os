/*
 * Zen OS — Session Manager
 * PAM authentication + logind session + compositor launch
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#endif

#include <systemd/sd-bus.h>

#include "zen/session.h"

/* ── Internal state ──────────────────────────────────────────────────────── */

static char  s_session_id[256];
static sd_bus *s_bus = NULL;

/* ── OOBE detection ──────────────────────────────────────────────────────── */

int zen_session_oobe_complete(void) {
    return access("/var/lib/zenos/oobe-complete", F_OK) == 0 ? 1 : 0;
}

/* ── PAM conversation function ───────────────────────────────────────────── */

#ifdef HAVE_PAM
static int pam_conv_fn(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *appdata_ptr) {
    (void)appdata_ptr;

    struct pam_response *reply = calloc((size_t)num_msg, sizeof(*reply));
    if (!reply)
        return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {

            /* Disable echo for password prompts */
            struct termios old_term, new_term;
            int echo_disabled = 0;

            if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
                if (tcgetattr(STDIN_FILENO, &old_term) == 0) {
                    new_term = old_term;
                    new_term.c_lflag &= ~(tcflag_t)ECHO;
                    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
                    echo_disabled = 1;
                }
            }

            char buf[512] = {0};
            if (fgets(buf, sizeof(buf), stdin)) {
                /* Strip trailing newline */
                size_t len = strlen(buf);
                if (len > 0 && buf[len - 1] == '\n')
                    buf[len - 1] = '\0';
                reply[i].resp = strdup(buf);
            } else {
                reply[i].resp = strdup("");
            }

            if (echo_disabled) {
                tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
                printf("\n");
            }

            if (!reply[i].resp) {
                /* Allocation failure — free what we have */
                for (int j = 0; j < i; j++)
                    free(reply[j].resp);
                free(reply);
                return PAM_BUF_ERR;
            }
        } else {
            reply[i].resp = NULL;
        }
    }

    *resp = reply;
    return PAM_SUCCESS;
}
#endif /* HAVE_PAM */

/* ── zen_session_init ────────────────────────────────────────────────────── */

int zen_session_init(void) {
#ifdef HAVE_PAM
    static const struct pam_conv conv = {
        .conv        = pam_conv_fn,
        .appdata_ptr = NULL,
    };

    int authenticated = 0;

    for (int attempt = 0; attempt < 3 && !authenticated; attempt++) {
        char username[256] = {0};
        char password[512] = {0};

        /* Read username */
        printf("Username: ");
        fflush(stdout);
        if (!fgets(username, sizeof(username), stdin))
            continue;
        size_t ulen = strlen(username);
        if (ulen > 0 && username[ulen - 1] == '\n')
            username[ulen - 1] = '\0';

        /* Read password with echo disabled */
        printf("Password: ");
        fflush(stdout);
        struct termios old_term, new_term;
        int echo_disabled = 0;
        if (tcgetattr(STDIN_FILENO, &old_term) == 0) {
            new_term = old_term;
            new_term.c_lflag &= ~(tcflag_t)ECHO;
            tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
            echo_disabled = 1;
        }
        if (fgets(password, sizeof(password), stdin)) {
            size_t plen = strlen(password);
            if (plen > 0 && password[plen - 1] == '\n')
                password[plen - 1] = '\0';
        }
        if (echo_disabled) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
            printf("\n");
        }

        pam_handle_t *pamh = NULL;
        int ret = pam_start("login", username, &conv, &pamh);
        if (ret != PAM_SUCCESS) {
            fprintf(stderr, "zen-session: pam_start failed: %s\n",
                    pam_strerror(pamh, ret));
            if (pamh)
                pam_end(pamh, ret);
            continue;
        }

        ret = pam_authenticate(pamh, 0);
        if (ret != PAM_SUCCESS) {
            fprintf(stderr, "zen-session: Authentication failed: %s\n",
                    pam_strerror(pamh, ret));
            pam_end(pamh, ret);
            continue;
        }

        pam_end(pamh, PAM_SUCCESS);
        authenticated = 1;
        /* Wipe password from stack */
        memset(password, 0, sizeof(password));
    }

    if (!authenticated) {
        fprintf(stderr, "zen-session: Too many failed authentication attempts\n");
        return -1;
    }

#else
    fprintf(stderr, "zen-session: WARNING — PAM not available, "
            "proceeding without authentication\n");
#endif /* HAVE_PAM */

    return 0;
}

/* ── zen_session_run ─────────────────────────────────────────────────────── */

void zen_session_run(void) {
    int ret;

    /* Connect to system bus for logind */
    ret = sd_bus_open_system(&s_bus);
    if (ret < 0) {
        fprintf(stderr, "zen-session: sd_bus_open_system failed: %s\n",
                strerror(-ret));
        /* Fall through with stub session_id */
    }

    /* Create logind session via org.freedesktop.login1.Manager.CreateSession */
    memset(s_session_id, 0, sizeof(s_session_id));

    if (s_bus) {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = NULL;

        /*
         * CreateSession requires many parameters. In a development build we
         * attempt the call and fall back to a stub session_id on failure.
         * The important structural element is that we use sd-bus correctly.
         */
        ret = sd_bus_call_method(
            s_bus,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "CreateSession",
            &error,
            &reply,
            /* uid, pid, service, type, class, desktop, seat_id,
               vtnr, tty, display, remote, remote_user, remote_host,
               properties */
            "uusssssussbssa(sv)",
            (uint32_t)getuid(),
            (uint32_t)getpid(),
            "zen-session",
            "wayland",
            "user",
            "zen-os",
            "",
            (uint32_t)0,
            "",
            "",
            0,
            "",
            "",
            (size_t)0
        );

        if (ret < 0) {
            fprintf(stderr,
                    "zen-session: CreateSession failed (%s), "
                    "using stub session_id\n",
                    error.message ? error.message : strerror(-ret));
            snprintf(s_session_id, sizeof(s_session_id), "zen-dev-session");
        } else {
            const char *id = NULL;
            /* First out-param of CreateSession is the session id string */
            if (sd_bus_message_read(reply, "s", &id) >= 0 && id) {
                snprintf(s_session_id, sizeof(s_session_id), "%s", id);
            } else {
                snprintf(s_session_id, sizeof(s_session_id), "zen-dev-session");
            }
            sd_bus_message_unref(reply);
        }

        sd_bus_error_free(&error);
    } else {
        snprintf(s_session_id, sizeof(s_session_id), "zen-dev-session");
    }

    fprintf(stderr, "zen-session: logind session_id = %s\n", s_session_id);

    /* Fork + exec zen-compositor */
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "zen-session: fork failed: %s\n", strerror(errno));
        return;
    }

    if (pid == 0) {
        /* Child: set up environment and exec compositor */
        setenv("XDG_SESSION_TYPE", "wayland", 1);
        setenv("XDG_SESSION_ID", s_session_id, 1);

        char *argv[] = { "zen-compositor", NULL };
        execvp("zen-compositor", argv);

        /* execvp only returns on failure */
        fprintf(stderr, "zen-session: execvp zen-compositor failed: %s\n",
                strerror(errno));
        _exit(127);
    }

    /* Parent: wait for compositor to exit */
    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        fprintf(stderr, "zen-session: compositor exited with code %d\n",
                WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "zen-session: compositor killed by signal %d\n",
                WTERMSIG(status));
    }

    /* Terminate the logind session */
    if (s_session_id[0] != '\0') {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "loginctl terminate-session %s",
                 s_session_id);
        int rc = system(cmd);
        if (rc != 0) {
            fprintf(stderr,
                    "zen-session: loginctl terminate-session returned %d\n",
                    rc);
        }
    }
}

/* ── zen_session_destroy ─────────────────────────────────────────────────── */

void zen_session_destroy(void) {
    if (s_bus) {
        sd_bus_unref(s_bus);
        s_bus = NULL;
    }
    memset(s_session_id, 0, sizeof(s_session_id));
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (zen_session_init() != 0)
        return 1;

    zen_session_run();
    zen_session_destroy();

    return 0;
}
