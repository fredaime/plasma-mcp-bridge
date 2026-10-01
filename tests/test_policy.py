# SPDX-License-Identifier: MIT
"""dbus_call guard rails: bus selection, built-in denylist, user rules, audit, startup."""
import unittest

import dbus

from fixtures.trap_services import (DECOY, DENIED, HIDDEN_PATH, LOGIN1, LOGIN1_MANAGER,
                                    LOGIN1_PATH, SYSTEMD1, SYSTEMD1_PATH, TRAP)
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


class TrapTestCase(PolicyTestCase):
    fixtures = ('fixtures/trap_services.py',)

    def setUp(self):
        self.trap = dbus.SessionBus().get_object(LOGIN1, '/Trap')
        self.trap.Reset(dbus_interface=TRAP)

    def calls(self):
        """What the stand-ins received since setUp, as "<path>|<interface>|<member>"."""
        return [str(call) for call in self.trap.Calls(dbus_interface=TRAP)]

    def login1(self, method, *flags, **extra):
        """dbus_call on the login1 stand-in from a fresh bridge started with flags."""
        arguments = dict({'service': LOGIN1, 'path': LOGIN1_PATH, 'method': method}, **extra)
        return self.bridge(*flags).call('dbus_call', arguments)


class BuiltinDenylist(TrapTestCase):

    def test_every_denied_member_is_refused(self):
        bridge = self.bridge()
        for service, path, interface, members in DENIED:
            for member in members:
                for given in (interface, None):
                    with self.subTest(service=service, member=member, interface=given):
                        arguments = {'service': service, 'path': path, 'method': member}
                        if given:
                            arguments['interface'] = given
                        self.assertRefused(bridge.call('dbus_call', arguments),
                                           'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_ambiguous_member_is_refused(self):
        # PowerOff is declared by the Manager and by the decoy interface.
        self.assertRefused(self.login1('PowerOff'), 'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_unresolvable_member_is_refused(self):
        self.assertRefused(self.login1('PowerOff', path=HIDDEN_PATH), 'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_same_member_on_another_interface_is_allowed(self):
        self.assertEqual(self.login1('PowerOff', interface=DECOY), ('decoy', False))
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + DECOY + '|PowerOff'])

    def test_harmless_members_are_allowed(self):
        self.assertEqual(self.login1('CanPowerOff'), ('yes', False))
        self.assertEqual(self.login1('CanPowerOff', interface=LOGIN1_MANAGER), ('yes', False))
        reply = self.bridge().call('dbus_call', {
            'service': SYSTEMD1, 'path': SYSTEMD1_PATH, 'method': 'GetDefaultTarget'})
        self.assertEqual(reply, ('graphical.target', False))

    def test_resolved_interface_is_sent(self):
        # Without 'interface', the call carries the interface the policy judged.
        self.login1('CanPowerOff')
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + LOGIN1_MANAGER + '|CanPowerOff'])

    def test_bus_daemon_activation_environment_is_refused(self):
        reply = self.bridge().call('dbus_call', {
            'service': 'org.freedesktop.DBus', 'path': '/org/freedesktop/DBus',
            'interface': 'org.freedesktop.DBus', 'method': 'UpdateActivationEnvironment',
            'args': [{}]})
        self.assertRefused(reply, 'built-in denylist entry')

    def test_denylist_applies_on_the_system_bus(self):
        # The harness's system bus is the private bus: the stand-in is there too.
        reply = self.bridge('--allow-system-bus').call('dbus_call', {
            'bus': 'system', 'service': LOGIN1, 'path': LOGIN1_PATH,
            'interface': LOGIN1_MANAGER, 'method': 'PowerOff'})
        self.assertRefused(reply, 'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_unique_name_destination_is_refused(self):
        owner = str(dbus.SessionBus().get_name_owner(LOGIN1))
        bridge = self.bridge()
        for method in ('PowerOff', 'CanPowerOff'):
            with self.subTest(method=method):
                reply = bridge.call('dbus_call', {'service': owner, 'path': LOGIN1_PATH,
                                                  'interface': LOGIN1_MANAGER, 'method': method})
                self.assertRefused(reply, '--allow-unique-names')
        self.assertEqual(self.calls(), [])


if __name__ == '__main__':
    unittest.main()
