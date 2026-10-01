#!/bin/sh
# SPDX-License-Identifier: MIT
# Run a command on a throw-away private session bus.
#   tests/run_with_bus.sh python3 -m unittest -v test_smoke
# The system bus is redirected to the same private bus, so bus:"system" in a
# test can never reach the real system bus.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
exec dbus-run-session --config-file="$here/fixtures/session.conf" -- sh -c '
  export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
  export PLASMA_MCP_TEST_BUS="$DBUS_SESSION_BUS_ADDRESS"
  export LANG=C.UTF-8 LC_ALL=C.UTF-8
  exec "$@"' sh "$@"
