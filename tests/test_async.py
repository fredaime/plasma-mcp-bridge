# SPDX-License-Identifier: MIT
"""Tool calls off the main thread: concurrency, plugins, cancellation, timeouts, shutdown."""
import os
import unittest

from mcp_session import FixtureTestCase

TEST_PLUGIN = os.environ.get('PLASMA_MCP_TEST_PLUGIN', '')


class Plugins(FixtureTestCase):
    fixtures = ()

    def test_duplicate_tool_name_is_refused(self):
        session = self.bridge('--plugin', TEST_PLUGIN)
        tools = session.request('tools/list')['result']['tools']
        names = [tool['name'] for tool in tools]
        self.assertEqual(names.count('dbus_call'), 1, names)
        self.assertIn('test_sleep_a', names)
        call = next(tool for tool in tools if tool['name'] == 'dbus_call')
        self.assertTrue(call['description'].startswith('Invoke a method on any D-Bus object'))
        self.assertIn("refusing a second tool named 'dbus_call'", session.stderr_text())


if __name__ == '__main__':
    unittest.main()
