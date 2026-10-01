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


    def test_tool_descriptions_match_the_policy(self):
        tools = {t['name']: t for t in self.bridge().request('tools/list')['result']['tools']}
        call = tools['dbus_call']
        self.assertNotIn('portal', call['description'])
        self.assertIn('--allow', call['description'])
        self.assertNotIn('required for reliable type coercion',
                         call['inputSchema']['properties']['interface']['description'])
        for name in ('dbus_list_services', 'dbus_introspect', 'dbus_call'):
            with self.subTest(tool=name):
                self.assertIn('--allow-system-bus',
                              tools[name]['inputSchema']['properties']['bus']['description'])


if __name__ == '__main__':
    unittest.main()
