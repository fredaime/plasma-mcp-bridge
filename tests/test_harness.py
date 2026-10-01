# SPDX-License-Identifier: MIT
"""The harness must protect the user's buses and never hang."""
import os
import subprocess
import sys
import threading
import time
import unittest

import dbus

from mcp_session import ECHO, HERE, DBusFixture, FixtureTestCase, MCPTimeout


class Harness(FixtureTestCase):

    def test_refuses_outside_private_bus(self):
        env = dict(os.environ)
        env.pop('PLASMA_MCP_TEST_BUS')
        probe = subprocess.run(
            [sys.executable, '-c', 'import mcp_session; mcp_session.MCPSession()'],
            cwd=HERE, env=env, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(probe.returncode, 0)
        self.assertIn('refusing to run outside the private test bus', probe.stderr)

    def test_system_bus_is_the_private_bus(self):
        names = self.bridge('--allow-system-bus').call_json('dbus_list_services', {'bus': 'system'})
        self.assertIn('org.plasmamcp.Validation', names)
        self.assertNotIn('org.freedesktop.login1', names)

    def test_nothing_is_activatable(self):
        names = self.bridge().call_json('dbus_call', {
            'service': 'org.freedesktop.DBus', 'path': '/org/freedesktop/DBus',
            'method': 'ListActivatableNames'})
        self.assertEqual(names, ['org.freedesktop.DBus'])

    def test_timeout_kills_the_bridge(self):
        session = self.bridge()
        started = time.monotonic()
        with self.assertRaises(MCPTimeout):
            session.call('dbus_call', dict(ECHO, method='Slow', args=[5]), timeout=0.5)
        self.assertLess(time.monotonic() - started, 2.0)
        self.assertIsNotNone(session.proc.poll())

    def test_fixture_never_ready(self):
        started = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, 'did not report READY'):
            DBusFixture(os.path.join(HERE, 'fixtures/never_ready.py'), timeout=1.0)
        self.assertLess(time.monotonic() - started, 4.0)

    def test_oracle_slow_overlaps(self):
        # Slow() must not serialise the service, or concurrency tests would
        # measure the oracle instead of the bridge.
        def one():
            proxy = dbus.SessionBus(private=True).get_object(ECHO['service'], ECHO['path'])
            proxy.Slow(1.0, dbus_interface=ECHO['interface'])
        started = time.monotonic()
        threads = [threading.Thread(target=one) for _ in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(5)
        self.assertLess(time.monotonic() - started, 1.8)


if __name__ == '__main__':
    unittest.main()
