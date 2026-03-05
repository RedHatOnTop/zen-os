/*
 * Zen OS — Privacy Guard Daemon
 *
 * System-level telemetry blocking and privacy enforcement.
 * Manages nftables rules and DNS filtering to block tracking domains.
 * Maintains an audit log of all blocked requests.
 * Exposes org.zenos.PrivacyGuard D-Bus interface.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZEN_PRIVACY_GUARD_PRIVACY_GUARD_H
#define ZEN_PRIVACY_GUARD_PRIVACY_GUARD_H

/*
 * Initialize the Privacy Guard daemon.
 * Loads blocklists, configures nftables rules, connects to sd-bus.
 * Returns 0 on success, -1 on failure.
 */
int zen_privacy_guard_init(void);

/*
 * Run the Privacy Guard event loop.  Blocks until terminated.
 */
void zen_privacy_guard_run(void);

/*
 * Shut down the Privacy Guard, releasing all resources.
 */
void zen_privacy_guard_destroy(void);

#endif /* ZEN_PRIVACY_GUARD_PRIVACY_GUARD_H */
