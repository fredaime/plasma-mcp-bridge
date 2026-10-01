# SPDX-License-Identifier: MIT
"""Protocol basics on the private bus."""
import unittest

from mcp_session import FixtureTestCase


class Smoke(FixtureTestCase):
    fixtures = ()

    def test_initialize(self):
        result = self.bridge().init_result
        self.assertEqual(result['serverInfo']['name'], 'plasma-mcp-bridge')
        self.assertIn('tools', result['capabilities'])
        self.assertEqual(result['protocolVersion'], '2024-11-05')

    def test_tools_list(self):
        tools = self.bridge().request('tools/list')['result']['tools']
        self.assertEqual([t['name'] for t in tools],
                         ['dbus_list_services', 'dbus_introspect', 'dbus_call', 'desktop_notify'])

    def test_ping(self):
        self.assertEqual(self.bridge().request('ping')['result'], {})

    def test_eof_exits_cleanly(self):
        self.assertEqual(self.bridge().close(timeout=2.0), 0)


if __name__ == '__main__':
    unittest.main()
