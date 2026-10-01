#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""D-Bus oracle for the plasma-mcp-bridge test-suite.

Owns org.plasmamcp.Validation at /Echo on the private test bus.
- Echo<X>(...)  returns "<wire signature>|<repr of the received args>", so a
                test sees exactly what the bridge put on the wire.
- Ret<X>()      returns a fixed value of a given D-Bus type.
- Slow(d)       replies after d seconds WITHOUT blocking the service (several
                Slow calls overlap); SlowBlocking(d) blocks the service.
- org.freedesktop.DBus.Properties Set/Get/GetAll record the last Set.
- /Loose answers EchoAny(any signature) but has no introspection data.
Prints "READY org.plasmamcp.Validation" once the name is owned.
"""
import os
import sys
import time

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

NAME = 'org.plasmamcp.Validation'
IFACE = NAME
PROPS = 'org.freedesktop.DBus.Properties'


def _echo(signature):
    """Echo<X>: replies "<wire signature>|<repr of the received args>"."""
    names = ['a%d' % i for i in range(len(list(dbus.Signature(signature))))]

    def decorator(fn):
        namespace = {}
        # dbus-python needs one named parameter per argument of the signature.
        exec('def method(self, %s, msg=None):\n'
             '    return "%%s|%%r" %% (msg.get_signature(), [%s])\n'
             % (', '.join(names), ', '.join(names)), namespace)
        method = namespace['method']
        method.__name__ = fn.__name__
        return dbus.service.method(IFACE, in_signature=signature, out_signature='s',
                                   message_keyword='msg')(method)
    return decorator


def _dup(interface):
    """Same member name in two interfaces: calls without an interface are ambiguous."""
    def Dup(self, a, msg=None):
        return '%s|%s|%r' % (msg.get_interface(), msg.get_signature(), [a])
    return dbus.service.method(interface, in_signature='u', out_signature='s',
                               message_keyword='msg')(Dup)


class _OtherInterface(dbus.service.Object):
    # dbus-python looks a member up by attribute name, class by class along the
    # MRO: the second declaration of Dup must live in a base class.
    Dup = _dup('org.plasmamcp.Other')


class Echo(_OtherInterface):
    last_set = 'never'

    # --- Echo: what did the bridge send? -------------------------------
    @_echo('au')
    def EchoAU(self): pass
    @_echo('a{ss}')
    def EchoASS(self): pass
    @_echo('(si)')
    def EchoStruct(self): pass
    @_echo('ay')
    def EchoAY(self): pass
    @_echo('o')
    def EchoO(self): pass
    @_echo('v')
    def EchoV(self): pass
    @_echo('u')
    def EchoU(self): pass
    @_echo('n')
    def EchoN(self): pass
    @_echo('x')
    def EchoX(self): pass
    @_echo('d')
    def EchoD(self): pass
    @_echo('as')
    def EchoAS(self): pass
    @_echo('a{sv}')
    def EchoASV(self): pass
    @_echo('y')
    def EchoY(self): pass
    @_echo('b')
    def EchoB(self): pass
    @_echo('ox')
    def EchoOX(self): pass
    @_echo('t')
    def EchoT(self): pass
    @_echo('s')
    def EchoS(self): pass
    @_echo('i')
    def EchoI(self): pass
    @_echo('ai')
    def EchoAI(self): pass
    @_echo('ax')
    def EchoAX(self): pass
    @_echo('at')
    def EchoAT(self): pass
    @_echo('ad')
    def EchoAD(self): pass
    @_echo('ab')
    def EchoAB(self): pass
    @_echo('g')
    def EchoG(self): pass
    @_echo('h')
    def EchoH(self): pass
    @_echo('a(si)')
    def EchoASI(self): pass
    @_echo('a(ai)')
    def EchoAStructAI(self): pass
    @_echo('aas')
    def EchoAAS(self): pass
    @_echo('aay')
    def EchoAAY(self): pass
    @_echo('aai')
    def EchoAAI(self): pass
    @_echo('aa{sv}')
    def EchoAASV(self): pass
    @_echo('a{sa{sv}}')
    def EchoASASV(self): pass
    @_echo('a{sa{ss}}')
    def EchoASASS(self): pass
    @_echo('asaiu')
    def EchoASAIU(self): pass
    @_echo('(a(si)v)')
    def EchoNested(self): pass
    @_echo('a{iu}')
    def EchoMapIU(self): pass
    @_echo('a(sssuda{sv})')
    def EchoUnsupported(self): pass
    @_echo('a{s(sssuda{sv})}')
    def EchoMapUnsupported(self): pass

    @dbus.service.method(IFACE, in_signature='u', out_signature='s', message_keyword='msg')
    def EchoIface(self, a, msg=None):
        return '%s|%s|%r' % (msg.get_interface(), msg.get_signature(), [a])

    Dup = _dup(IFACE)

    # --- Ret: typed replies -------------------------------------------------
    @dbus.service.method(IFACE, in_signature='', out_signature='u')
    def RetU(self): return dbus.UInt32(123456789)
    @dbus.service.method(IFACE, in_signature='', out_signature='x')
    def RetX(self): return dbus.Int64(4000000)
    @dbus.service.method(IFACE, in_signature='', out_signature='ax')
    def RetAXBig(self): return [2 ** 53, 2 ** 53 + 1, -(2 ** 53 + 1)]
    @dbus.service.method(IFACE, in_signature='', out_signature='d')
    def RetD(self): return dbus.Double(3.14159265)
    @dbus.service.method(IFACE, in_signature='', out_signature='t')
    def RetT(self): return dbus.UInt64(18446744073709551615)
    @dbus.service.method(IFACE, in_signature='', out_signature='ay')
    def RetAY(self): return dbus.ByteArray(b'hello')
    @dbus.service.method(IFACE, in_signature='', out_signature='h')
    def RetH(self): return dbus.types.UnixFd(os.open('/dev/null', os.O_RDONLY))
    @dbus.service.method(IFACE, in_signature='', out_signature='')
    def RetVoid(self): return None
    @dbus.service.method(IFACE, in_signature='', out_signature='a{sv}')
    def RetASV(self):
        return {'pos': dbus.Int64(123456789), 'pi': dbus.Double(3.14159265),
                'pid': dbus.UInt32(3303203)}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{sv}')
    def RetASVBigT(self):
        return {'big': dbus.UInt64(18446744073709551615, variant_level=1),
                'small': dbus.UInt64(5, variant_level=1)}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{sv}')
    def RetBigASV(self):
        return {'k%04d' % i: dbus.Int32(i, variant_level=1) for i in range(5000)}
    @dbus.service.method(IFACE, in_signature='', out_signature='a(iss)')
    def RetAStructISS(self): return [(1, 'a', 'b'), (2, 'c', 'd')]
    @dbus.service.method(IFACE, in_signature='', out_signature='aai')
    def RetAAI(self): return [[1, 2], [3]]
    @dbus.service.method(IFACE, in_signature='', out_signature='a(ai)')
    def RetAStructAI(self): return [([1, 2],)]
    @dbus.service.method(IFACE, in_signature='', out_signature='(ao)')
    def RetStructAO(self): return ([dbus.ObjectPath('/a')],)
    @dbus.service.method(IFACE, in_signature='', out_signature='av')
    def RetAVofAS(self): return [dbus.Array(['a'], signature='s', variant_level=1)]
    @dbus.service.method(IFACE, in_signature='', out_signature='ao')
    def RetAO(self): return [dbus.ObjectPath('/a'), dbus.ObjectPath('/b/c')]
    @dbus.service.method(IFACE, in_signature='', out_signature='ag')
    def RetAG(self): return [dbus.Signature('i'), dbus.Signature('a{sv}')]
    # B1: string / byte arrays nested in containers
    @dbus.service.method(IFACE, in_signature='', out_signature='aas')
    def RetAAS(self): return [['a', 'b'], ['c']]
    @dbus.service.method(IFACE, in_signature='', out_signature='aas')
    def RetAASWithEmpty(self): return [dbus.Array([], signature='s'), ['x']]
    @dbus.service.method(IFACE, in_signature='', out_signature='(as)')
    def RetStructAS(self): return (['a', 'b'],)
    @dbus.service.method(IFACE, in_signature='', out_signature='aay')
    def RetAAY(self): return [dbus.ByteArray(b'hi')]
    @dbus.service.method(IFACE, in_signature='', out_signature='a{sas}')
    def RetMapAS(self): return {'k': ['a']}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{say}')
    def RetMapAY(self): return {'k': dbus.ByteArray(b'hi')}
    @dbus.service.method(IFACE, in_signature='', out_signature='as')
    def RetEmptyAS(self): return dbus.Array([], signature='s')
    @dbus.service.method(IFACE, in_signature='', out_signature='a{sas}')
    def RetEmptyMapAS(self): return dbus.Dictionary({}, signature='sas')
    @dbus.service.method(IFACE, in_signature='', out_signature='ay')
    def RetEmptyAY(self): return dbus.ByteArray(b'')
    # M7: non-string map keys
    @dbus.service.method(IFACE, in_signature='', out_signature='a{oas}')
    def RetMapOAS(self): return {dbus.ObjectPath('/p'): ['x']}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{gas}')
    def RetMapGAS(self): return {dbus.Signature('a{sv}'): ['x']}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{is}')
    def RetMapIS(self): return {1: 'a', 2: 'b'}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{bs}')
    def RetMapBS(self): return {True: 'y', False: 'n'}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{ys}')
    def RetMapYS(self): return {dbus.Byte(0): 'a', dbus.Byte(65): 'b', dbus.Byte(200): 'c'}

    # --- Slow ---------------------------------------------------------------
    @dbus.service.method(IFACE, in_signature='d', out_signature='s',
                         async_callbacks=('reply', 'error'))
    def Slow(self, seconds, reply, error):
        GLib.timeout_add(int(seconds * 1000), lambda: reply('slept %.1fs' % seconds) or False)

    @dbus.service.method(IFACE, in_signature='d', out_signature='s')
    def SlowBlocking(self, seconds):
        time.sleep(seconds)
        return 'slept %.1fs' % seconds

    # --- Properties ---------------------------------------------------------
    @dbus.service.method(PROPS, in_signature='ssv', out_signature='', message_keyword='msg')
    def Set(self, iface, prop, value, msg=None):
        Echo.last_set = '%s|%s.%s=%r' % (msg.get_signature(), iface, prop, value)

    @dbus.service.method(PROPS, in_signature='ss', out_signature='v')
    def Get(self, iface, prop):
        return Echo.last_set

    @dbus.service.method(PROPS, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        return {'LastSet': Echo.last_set}


NESTED_XML = '''<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect"><arg name="data" type="s" direction="out"/></method>
  </interface>
  <interface name="org.plasmamcp.Parent">
    <method name="Nested"><arg type="u" direction="in"/><arg type="s" direction="out"/></method>
  </interface>
  <node name="child">
    <interface name="org.plasmamcp.Child">
      <method name="Nested"><arg type="s" direction="in"/><arg type="s" direction="out"/></method>
    </interface>
  </node>
</node>'''


class Nested(dbus.service.Object):
    """Introspection data with a full child description (allowed by the spec):
    the child's interfaces must not be attributed to this object."""

    @dbus.service.method('org.freedesktop.DBus.Introspectable', in_signature='', out_signature='s')
    def Introspect(self):
        return NESTED_XML

    @dbus.service.method('org.plasmamcp.Parent', in_signature='u', out_signature='s',
                         message_keyword='msg')
    def Nested(self, a, msg=None):
        return '%s|%s|%r' % (msg.get_interface(), msg.get_signature(), [a])


_BASIC_CODES = {dbus.Byte: 'y', dbus.Boolean: 'b', dbus.Int16: 'n', dbus.UInt16: 'q',
                dbus.Int32: 'i', dbus.UInt32: 'u', dbus.Int64: 'x', dbus.UInt64: 't',
                dbus.Double: 'd', dbus.String: 's', dbus.ObjectPath: 'o',
                dbus.Signature: 'g'}


def _wire_type(value):
    if isinstance(value, dbus.Dictionary):
        return 'a{%s}' % value.signature
    if isinstance(value, dbus.Array):
        return 'a%s' % value.signature
    return _BASIC_CODES[type(value)]


class Loose(dbus.service.Object):
    """No introspection data: the bridge cannot type the arguments, so they
    take the natural mapping. EchoAny accepts any signature."""

    @dbus.service.method('org.freedesktop.DBus.Introspectable', in_signature='',
                         out_signature='s')
    def Introspect(self):
        return '<node/>'

    # dbus-python cannot pass the message to a *args method: the signature is
    # rebuilt from the types the arguments arrived with.
    @dbus.service.method(IFACE, out_signature='s')
    def EchoAny(self, *args):
        return '%s|%r' % (''.join(_wire_type(a) for a in args), list(args))


def main():
    if os.environ.get('PLASMA_MCP_TEST_BUS') != os.environ.get('DBUS_SESSION_BUS_ADDRESS'):
        sys.exit('echo_service: refusing to own names outside the private test bus '
                 '(run through tests/run_with_bus.sh)')
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    name = dbus.service.BusName(NAME, bus, do_not_queue=True)  # noqa: F841 (keeps the name)
    Echo(bus, '/Echo')
    Nested(bus, '/Nested')
    Loose(bus, '/Loose')
    print('READY ' + NAME, flush=True)
    GLib.MainLoop().run()


if __name__ == '__main__':
    main()
