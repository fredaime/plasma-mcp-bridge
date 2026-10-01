# SPDX-License-Identifier: MIT
"""dbus_call guard rails: bus selection, built-in denylist, user rules, audit, startup."""
import unittest

from mcp_session import ECHO, FixtureTestCase


class PolicyTestCase(FixtureTestCase):

    def assertRefused(self, reply, hint):
        self.assertTrue(reply.is_error, reply.text)
        self.assertTrue(reply.text.startswith('Refused by policy'), reply.text)
        self.assertIn(hint, reply.text)


SYSTEM_CALLS = (
    ('dbus_list_services', {'bus': 'system'}),
    ('dbus_introspect', {'bus': 'system', 'service': ECHO['service'], 'path': ECHO['path']}),
    ('dbus_call', dict(ECHO, bus='system', method='RetU')),
)


class Buses(PolicyTestCase):

    def test_system_bus_refused_by_default(self):
        bridge = self.bridge()
        for tool, arguments in SYSTEM_CALLS:
            with self.subTest(tool=tool):
                self.assertRefused(bridge.call(tool, arguments), '--allow-system-bus')

    def test_system_bus_with_flag(self):
        bridge = self.bridge('--allow-system-bus')
        for tool, arguments in SYSTEM_CALLS:
            with self.subTest(tool=tool):
                reply = bridge.call(tool, arguments)
                self.assertFalse(reply.is_error, reply.text)
        self.assertIn(ECHO['service'], bridge.call_json('dbus_list_services', {'bus': 'system'}))

    def test_session_bus_is_the_default(self):
        bridge = self.bridge()
        self.assertEqual(bridge.call('dbus_call', dict(ECHO, method='RetU')), ('123456789', False))
        # A client may send null for "not set".
        self.assertEqual(bridge.call('dbus_call', dict(ECHO, bus=None, method='RetU')),
                         ('123456789', False))

    def test_unknown_bus_is_an_error(self):
        bridge = self.bridge('--allow-system-bus')
        for bus in ('sytem', 'SESSION', '', 5, True, ['session']):
            for tool, arguments in SYSTEM_CALLS:
                with self.subTest(tool=tool, bus=bus):
                    reply = bridge.call(tool, dict(arguments, bus=bus))
                    self.assertTrue(reply.is_error, reply.text)
                    self.assertIn('\'bus\' must be "session" or "system"', reply.text)


if __name__ == '__main__':
    unittest.main()
