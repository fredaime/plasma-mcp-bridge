# SPDX-License-Identifier: MIT
"""How dbus_call builds the message: interface resolution, void replies, errors."""
import unittest

from mcp_session import ECHO, FixtureTestCase

NO_IFACE = {'service': ECHO['service'], 'path': ECHO['path']}


class CallPath(FixtureTestCase):

    def call(self, **args):
        return self.bridge().call('dbus_call', args)

    def test_unique_method_gets_its_interface(self):
        reply = self.call(**NO_IFACE, method='EchoIface', args=[5])
        self.assertFalse(reply.is_error, reply.text)
        self.assertTrue(reply.text.startswith('org.plasmamcp.Validation|'), reply.text)

    def test_ambiguous_method_keeps_interface_empty(self):
        reply = self.call(**NO_IFACE, method='Dup', args=[5])
        self.assertFalse(reply.is_error, reply.text)
        self.assertTrue(reply.text.startswith('None|'), reply.text)

    def test_given_interface_resolves_ambiguity(self):
        reply = self.call(**NO_IFACE, interface='org.plasmamcp.Other', method='Dup', args=[5])
        self.assertTrue(reply.text.startswith('org.plasmamcp.Other|'), reply.text)

    def test_void_with_interface_is_null(self):
        self.assertEqual(self.call(**ECHO, method='RetVoid'), ('null', False))

    def test_void_without_interface_is_null(self):
        self.assertEqual(self.call(**NO_IFACE, method='RetVoid'), ('null', False))

    def test_bus_daemon_with_its_interface(self):
        reply = self.call(service='org.freedesktop.DBus', path='/org/freedesktop/DBus',
                          interface='org.freedesktop.DBus', method='GetConnectionUnixProcessID',
                          args=['org.plasmamcp.Validation'])
        self.assertFalse(reply.is_error, reply.text)
        self.assertTrue(reply.text.isdigit(), reply.text)

    def test_unknown_method_reports_remote_error(self):
        reply = self.call(**ECHO, method='NoSuchMethod')
        self.assertTrue(reply.is_error)
        self.assertIn('org.freedesktop.DBus.Error.UnknownMethod', reply.text)

    def test_unknown_object_path_reports_remote_error(self):
        reply = self.call(service=ECHO['service'], path='/NoSuchObject', method='RetU')
        self.assertTrue(reply.is_error)
        self.assertIn('org.freedesktop.DBus.Error.', reply.text)


if __name__ == '__main__':
    unittest.main()
