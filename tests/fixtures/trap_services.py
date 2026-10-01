#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stand-ins for the services of the built-in denylist (policy tests).

Owns the bus names of DENIED on the private test bus, on one connection.
Every member records "<path>|<interface>|<member>" and replies a string;
org.plasmamcp.Trap at /Trap returns the record (Calls) and clears it (Reset).
- On /org/freedesktop/login1, PowerOff and Ping also exist on the decoy
  interface org.plasmamcp.Decoy: without 'interface' they are ambiguous.
- /org/freedesktop/login1/hidden answers PowerOff, but its introspection
  data is empty: the interface cannot be resolved.
Prints "READY org.freedesktop.login1" once every name is owned.
"""
import os
import sys

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

LOGIN1 = 'org.freedesktop.login1'
LOGIN1_PATH = '/org/freedesktop/login1'
LOGIN1_MANAGER = 'org.freedesktop.login1.Manager'
SYSTEMD1 = 'org.freedesktop.systemd1'
SYSTEMD1_PATH = '/org/freedesktop/systemd1'
SYSTEMD1_MANAGER = 'org.freedesktop.systemd1.Manager'
DECOY = 'org.plasmamcp.Decoy'
TRAP = 'org.plasmamcp.Trap'
HIDDEN_PATH = LOGIN1_PATH + '/hidden'

# (bus name, object path, interface, members): every member must be refused,
# with and without 'interface'. The wildcard entries of the denylist are
# covered by the concrete members they must catch.
DENIED = (
    (LOGIN1, LOGIN1_PATH, LOGIN1_MANAGER, (
        'PowerOff', 'PowerOffWithFlags', 'Reboot', 'RebootWithFlags', 'Halt', 'HaltWithFlags',
        'Suspend', 'SuspendWithFlags', 'Hibernate', 'HibernateWithFlags', 'HybridSleep',
        'HybridSleepWithFlags', 'SuspendThenHibernate', 'SuspendThenHibernateWithFlags',
        'Sleep', 'TerminateSession', 'TerminateUser', 'TerminateSeat', 'KillSession',
        'KillUser', 'ScheduleShutdown', 'SetWallMessage')),
    (LOGIN1, LOGIN1_PATH + '/session/auto', 'org.freedesktop.login1.Session', (
        'Terminate', 'Kill')),
    (LOGIN1, LOGIN1_PATH + '/user/self', 'org.freedesktop.login1.User', ('Terminate', 'Kill')),
    ('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting', (
        'loadScript', 'loadDeclarativeScript', 'unloadScript', 'start')),
    ('org.kde.ksmserver', '/KSMServer', 'org.kde.KSMServerInterface', ('closeSession', 'logout')),
    ('org.kde.Shutdown', '/Shutdown', 'org.kde.Shutdown', (
        'logout', 'logoutAndShutdown', 'logoutAndReboot')),
    ('org.kde.plasmashell', '/PlasmaShell', 'org.kde.PlasmaShell', ('evaluateScript',)),
    (SYSTEMD1, SYSTEMD1_PATH, SYSTEMD1_MANAGER, (
        'StartUnit', 'StartUnitWithFlags', 'StartUnitReplace', 'StartTransientUnit',
        'RestartUnit', 'ReloadOrRestartUnit', 'EnqueueUnitJob', 'KillUnit', 'KillUnitSubgroup',
        'SetEnvironment', 'UnsetAndSetEnvironment', 'PowerOff', 'Reboot', 'SoftReboot', 'Halt',
        'KExec', 'Exit', 'SwitchRoot', 'QueueSignalUnit', 'StopUnit')),
    (SYSTEMD1, SYSTEMD1_PATH + '/unit/app_2eservice', 'org.freedesktop.systemd1.Unit', (
        'Start', 'Restart', 'ReloadOrRestart', 'Kill', 'KillSubgroup', 'QueueSignal', 'Stop',
        'EnqueueJob')),
)

# (object path, interface, member, reply): members the denylist lets through.
HARMLESS = (
    (LOGIN1_PATH, LOGIN1_MANAGER, 'CanPowerOff', 'yes'),
    (LOGIN1_PATH, LOGIN1_MANAGER, 'Ping', 'manager'),
    (LOGIN1_PATH, DECOY, 'Ping', 'decoy'),
    (LOGIN1_PATH, DECOY, 'PowerOff', 'decoy'),
    (SYSTEMD1_PATH, SYSTEMD1_MANAGER, 'GetDefaultTarget', 'graphical.target'),
)

CALLS = []


def _member(interface, name, reply):
    def method(self, msg=None):
        CALLS.append('%s|%s|%s' % (msg.get_path(), msg.get_interface(), msg.get_member()))
        return reply
    method.__name__ = name
    return dbus.service.method(interface, in_signature='', out_signature='s',
                               message_keyword='msg')(method)


def _object_class(path, interfaces):
    """One class per interface, each deriving from the previous: dbus-python
    looks a member up by attribute name, class by class along the MRO, so a
    name declared by two interfaces must live in two classes."""
    cls = dbus.service.Object
    for index, (interface, members) in enumerate(sorted(interfaces.items())):
        namespace = {name: _member(interface, name, reply) for name, reply in members}
        cls = type('Trap%d%s' % (index, path.replace('/', '_')), (cls,), namespace)
    return cls


class Hidden(dbus.service.Object):
    """Answers PowerOff, but its introspection data declares nothing."""

    @dbus.service.method('org.freedesktop.DBus.Introspectable', in_signature='',
                         out_signature='s')
    def Introspect(self):
        return '<node/>'

    PowerOff = _member(LOGIN1_MANAGER, 'PowerOff', 'called')


class Trap(dbus.service.Object):

    @dbus.service.method(TRAP, in_signature='', out_signature='as')
    def Calls(self):
        return CALLS

    @dbus.service.method(TRAP, in_signature='', out_signature='')
    def Reset(self):
        del CALLS[:]


def main():
    if os.environ.get('PLASMA_MCP_TEST_BUS') != os.environ.get('DBUS_SESSION_BUS_ADDRESS'):
        sys.exit('trap_services: refusing to own names outside the private test bus '
                 '(run through tests/run_with_bus.sh)')
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    objects = {}
    for _, path, interface, members in DENIED:
        objects.setdefault(path, {}).setdefault(interface, []).extend(
            (member, 'called') for member in members)
    for path, interface, member, reply in HARMLESS:
        objects.setdefault(path, {}).setdefault(interface, []).append((member, reply))
    keep = [dbus.service.BusName(name, bus, do_not_queue=True)
            for name in sorted({entry[0] for entry in DENIED})]
    keep += [_object_class(path, interfaces)(bus, path)
             for path, interfaces in sorted(objects.items())]
    keep += [Hidden(bus, HIDDEN_PATH), Trap(bus, '/Trap')]
    print('READY ' + LOGIN1, flush=True)
    GLib.MainLoop().run()


if __name__ == '__main__':
    main()
