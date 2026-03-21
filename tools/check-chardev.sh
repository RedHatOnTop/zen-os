#!/bin/bash
# Check QMP chardev status for the agent socket
SOCK="/tmp/zen-test-vms/debug-agent/qmp.sock"

# QMP handshake + query-chardev
(
echo '{"execute":"qmp_capabilities"}'
sleep 0.5
echo '{"execute":"query-chardev"}'
sleep 0.5
) | socat - UNIX-CONNECT:"$SOCK" 2>/dev/null | python3 -m json.tool 2>/dev/null || \
(
echo '{"execute":"qmp_capabilities"}'
sleep 0.5
echo '{"execute":"query-chardev"}'
sleep 0.5
) | socat - UNIX-CONNECT:"$SOCK" 2>/dev/null
