# SPDX-License-Identifier: MIT
"""dbus_call guard rails: bus selection, built-in denylist, user rules, audit, startup."""
import os
import re
import subprocess
import unittest

import dbus

from fixtures.trap_services import (DECOY, DENIED, HIDDEN_PATH, LOGIN1, LOGIN1_MANAGER,
                                    LOGIN1_PATH, SYSTEMD1, SYSTEMD1_PATH, TRAP)
from mcp_session import ECHO, FixtureTestCase, require_private_bus

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


MANAGER_POWEROFF = LOGIN1 + ':' + LOGIN1_MANAGER + '.PowerOff'
CAN_POWEROFF = LOGIN1 + ':' + LOGIN1_MANAGER + '.CanPowerOff'


class UserRules(TrapTestCase):

    def test_allow_lifts_a_builtin_entry(self):
        reply = self.login1('PowerOff', '--allow', MANAGER_POWEROFF, interface=LOGIN1_MANAGER)
        self.assertEqual(reply, ('called', False))
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + LOGIN1_MANAGER + '|PowerOff'])

    def test_allow_does_not_match_an_unknown_interface(self):
        # Without 'interface', PowerOff is ambiguous: the rule names an interface.
        self.assertRefused(self.login1('PowerOff', '--allow', MANAGER_POWEROFF),
                           'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_allow_with_any_interface_matches_an_unknown_interface(self):
        reply = self.login1('PowerOff', '--allow', LOGIN1 + ':*.PowerOff')
        self.assertFalse(reply.is_error, reply.text)
        self.assertEqual(len(self.calls()), 1)

    def test_deny_beats_allow(self):
        reply = self.login1('CanPowerOff', '--deny', CAN_POWEROFF, '--allow', CAN_POWEROFF)
        self.assertRefused(reply, '--deny ' + CAN_POWEROFF)
        self.assertEqual(self.calls(), [])

    def test_deny_wildcard(self):
        bridge = self.bridge('--deny', LOGIN1 + ':' + LOGIN1_MANAGER + '.Can*')
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'method': 'CanPowerOff'})
        self.assertRefused(reply, '--deny ' + LOGIN1 + ':' + LOGIN1_MANAGER + '.Can*')
        reply = bridge.call('dbus_call', {'service': SYSTEMD1, 'path': SYSTEMD1_PATH,
                                          'method': 'GetDefaultTarget'})
        self.assertEqual(reply, ('graphical.target', False))

    def test_deny_matches_an_unknown_interface(self):
        # Ping is declared by the Manager and by the decoy.
        reply = self.login1('Ping', '--deny', LOGIN1 + ':' + LOGIN1_MANAGER + '.Ping')
        self.assertRefused(reply, '--deny')
        self.assertEqual(self.calls(), [])

    def test_patterns_are_case_sensitive(self):
        reply = self.login1('CanPowerOff', '--deny', LOGIN1 + ':' + LOGIN1_MANAGER + '.canpoweroff')
        self.assertEqual(reply, ('yes', False))

    def test_default_deny_permits_only_allowed_calls(self):
        bridge = self.bridge('--default-deny', '--allow', CAN_POWEROFF)
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'method': 'CanPowerOff'})
        self.assertEqual(reply, ('yes', False))
        reply = bridge.call('dbus_call', {'service': SYSTEMD1, 'path': SYSTEMD1_PATH,
                                          'method': 'GetDefaultTarget'})
        self.assertRefused(reply, '--default-deny')
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'interface': LOGIN1_MANAGER, 'method': 'PowerOff'})
        self.assertRefused(reply, 'built-in denylist entry')
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + LOGIN1_MANAGER + '|CanPowerOff'])

    def test_default_deny_leaves_discovery(self):
        bridge = self.bridge('--default-deny')
        self.assertIn(LOGIN1, bridge.call_json('dbus_list_services'))
        reply = bridge.call('dbus_introspect', {'service': LOGIN1, 'path': LOGIN1_PATH})
        self.assertFalse(reply.is_error, reply.text)
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'method': 'CanPowerOff'})
        self.assertRefused(reply, '--default-deny')

    def test_allow_unique_names(self):
        owner = str(dbus.SessionBus().get_name_owner(LOGIN1))
        reply = self.bridge('--allow-unique-names').call('dbus_call', {
            'service': owner, 'path': LOGIN1_PATH, 'interface': LOGIN1_MANAGER,
            'method': 'CanPowerOff'})
        self.assertEqual(reply, ('yes', False))


def run_bridge(*args):
    """Runs the bridge with an empty stdin and returns the CompletedProcess."""
    return subprocess.run([os.environ['PLASMA_MCP_BRIDGE'], *args], stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=10)


class Startup(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        require_private_bus()

    def test_malformed_patterns_exit_2(self):
        for flag in ('--deny', '--allow'):
            for pattern in ('', 'org.kde.KWin', 'org.kde.KWin:loadScript',
                            ':org.kde.kwin.Scripting.loadScript',
                            'org.kde.KWin:org.kde.kwin.Scripting.', 'org.kde.KWin:.loadScript'):
                with self.subTest(flag=flag, pattern=pattern):
                    result = run_bridge(flag, pattern)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertIn(('invalid %s pattern' % flag).encode(), result.stderr)


AUDIT = 'plasma-mcp-bridge: audit: '
AUDIT_LINE = re.compile('^' + re.escape(AUDIT) + '.*$', re.MULTILINE)
RET_VOID = 'org.plasmamcp.Validation /Echo org.plasmamcp.Validation.RetVoid'
ECHO_RULE = 'org.plasmamcp.Validation:*.RetVoid'


class Audit(PolicyTestCase):

    def audited(self, session):
        return AUDIT_LINE.findall(session.stderr_text())

    def test_allowed_call(self):
        session = self.bridge()
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(self.audited(session), [AUDIT + 'allow session ' + RET_VOID])

    def test_resolved_interface(self):
        session = self.bridge()
        session.call('dbus_call', {'service': ECHO['service'], 'path': ECHO['path'],
                                   'method': 'RetVoid'})
        self.assertEqual(self.audited(session), [AUDIT + 'allow session ' + RET_VOID])

    def test_unknown_interface_is_a_star(self):
        session = self.bridge()
        session.call('dbus_call', {'service': ECHO['service'], 'path': ECHO['path'],
                                   'method': 'Dup', 'args': [5]})
        self.assertEqual(self.audited(session),
                         [AUDIT + 'allow session org.plasmamcp.Validation /Echo *.Dup'])

    def test_deny_rule(self):
        session = self.bridge('--deny', ECHO_RULE)
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(self.audited(session),
                         [AUDIT + 'deny session ' + RET_VOID + ' deny:' + ECHO_RULE])

    def test_allow_rule(self):
        session = self.bridge('--allow', ECHO_RULE)
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(self.audited(session),
                         [AUDIT + 'allow session ' + RET_VOID + ' allow:' + ECHO_RULE])

    def test_system_bus_refusal(self):
        session = self.bridge()
        session.call('dbus_call', dict(ECHO, bus='system', method='RetVoid'))
        self.assertEqual(self.audited(session),
                         [AUDIT + 'deny system ' + RET_VOID + ' system-bus'])

    def test_one_line_per_call(self):
        session = self.bridge()
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(len(self.audited(session)), 2)

    def test_read_only_tools_are_not_audited(self):
        session = self.bridge()
        session.call('dbus_list_services')
        session.call('dbus_introspect', {'service': ECHO['service'], 'path': ECHO['path']})
        self.assertEqual(self.audited(session), [])

    def test_invalid_calls_are_not_audited(self):
        session = self.bridge()
        session.call('dbus_call', {'service': ECHO['service'], 'path': ECHO['path']})
        session.call('dbus_call', dict(ECHO, bus='sytem', method='RetVoid'))
        self.assertEqual(self.audited(session), [])

    def test_names_cannot_forge_audit_lines(self):
        session = self.bridge()
        forged = 'RetVoid\n' + AUDIT + 'allow session x /x x.x'
        for field in ('service', 'path', 'interface', 'method'):
            with self.subTest(field=field):
                arguments = dict(ECHO, method='RetVoid')
                arguments[field] = forged
                reply = session.call('dbus_call', arguments)
                self.assertTrue(reply.is_error, reply.text)
                self.assertIn('may only contain the characters D-Bus allows', reply.text)
        reply = session.call('dbus_call', dict(ECHO, method='Ret Void'))
        self.assertIn('may only contain the characters D-Bus allows', reply.text)
        self.assertEqual(self.audited(session), [])


if __name__ == '__main__':
    unittest.main()
