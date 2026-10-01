# SPDX-License-Identifier: MIT
"""Arguments are converted to the declared D-Bus types, strictly (M6)."""
import random
import unittest

import dbus

from mcp_session import ECHO, FixtureTestCase

BYTES_HI = "ay|[dbus.Array([dbus.Byte(104), dbus.Byte(105)], signature=dbus.Signature('y'))]"


class Coercion(FixtureTestCase):

    def echo(self, method, value, interface=True):
        args = dict(ECHO, method=method, args=[value])
        if not interface:
            del args['interface']
        return self.bridge().call('dbus_call', args)

    def assertSent(self, method, value, expected, interface=True):
        reply = self.echo(method, value, interface)
        self.assertEqual(reply, (expected, False))

    def test_typed_scalars(self):
        self.assertSent('EchoU', 5, 'u|[dbus.UInt32(5)]')
        self.assertSent('EchoN', -5, 'n|[dbus.Int16(-5)]')
        self.assertSent('EchoY', 7, 'y|[dbus.Byte(7)]')
        self.assertSent('EchoX', 5, 'x|[dbus.Int64(5)]')
        self.assertSent('EchoD', 2, 'd|[dbus.Double(2.0)]')
        self.assertSent('EchoB', True, 'b|[dbus.Boolean(True)]')
        self.assertSent('EchoS', 'x', "s|[dbus.String('x')]")
        self.assertSent('EchoO', '/a/b', "o|[dbus.ObjectPath('/a/b')]")

    def test_typed_without_interface_when_unique(self):
        self.assertSent('EchoU', 5, 'u|[dbus.UInt32(5)]', interface=False)

    def test_integral_real_accepted(self):
        self.assertSent('EchoU', 5.0, 'u|[dbus.UInt32(5)]')

    def test_big_integers_as_strings(self):
        self.assertSent('EchoX', '9007199254740993', 'x|[dbus.Int64(9007199254740993)]')
        self.assertSent('EchoT', '18446744073709551615', 't|[dbus.UInt64(18446744073709551615)]')

    def test_big_integer_number_exact(self):
        # Qt keeps JSON integers that fit in int64 exactly: no rounding to 2^53.
        self.assertSent('EchoX', 9007199254740993, 'x|[dbus.Int64(9007199254740993)]')

    def test_int64_bounds_as_numbers(self):
        # toDouble() rounds these to +-2^63; Qt still holds them exactly.
        self.assertSent('EchoX', 9223372036854775807, 'x|[dbus.Int64(9223372036854775807)]')
        self.assertSent('EchoX', -9223372036854775808, 'x|[dbus.Int64(-9223372036854775808)]')

    def test_uint64_beyond_int64_needs_a_string(self):
        reply = self.echo('EchoT', 18446744073709551615)
        self.assertTrue(reply.is_error)
        self.assertIn('beyond the int64 range', reply.text)

    def test_rejections(self):
        cases = [
            ('EchoU', -1, 'argument 0 (u): -1 out of range [0,4294967295]'),
            ('EchoU', 5000000000, 'argument 0 (u): 5000000000 out of range [0,4294967295]'),
            ('EchoY', 300, 'argument 0 (y): 300 out of range [0,255]'),
            ('EchoN', 2.7, 'argument 0 (n): 2.7 is not an integer'),
            ('EchoU', 'junk', 'argument 0 (u): "junk" is not an integer'),
            ('EchoU', True, 'argument 0 (u): expected an integer, got true'),
            ('EchoB', 1, 'argument 0 (b): expected a boolean, got 1'),
            ('EchoD', 'x', 'argument 0 (d): expected a number, got "x"'),
            ('EchoS', 5, 'argument 0 (s): expected a string, got 5'),
            ('EchoO', 'not a path', 'argument 0 (o): "not a path" is not a valid object path'),
        ]
        for method, value, message in cases:
            with self.subTest(method=method, value=value):
                reply = self.echo(method, value)
                self.assertEqual(reply, (message, True))

    def test_byte_array_from_ints_or_base64(self):
        self.assertSent('EchoAY', [104, 105], BYTES_HI)
        self.assertSent('EchoAY', 'aGk=', BYTES_HI)

    def test_byte_array_rejections(self):
        self.assertEqual(self.echo('EchoAY', '!!!'),
                         ('argument 0 (ay): "!!!" is not valid base64', True))
        reply = self.echo('EchoAY', [256])
        self.assertTrue(reply.is_error)
        self.assertIn('out of range [0,255]', reply.text)

    def test_string_array_unchanged(self):
        self.assertSent('EchoAS', ['a', 'b'],
                        "as|[dbus.Array([dbus.String('a'), dbus.String('b')], "
                        "signature=dbus.Signature('s'))]")

    def test_variant_map_unchanged(self):
        reply = self.echo('EchoASV', {'a': 1, 'b': 'x'})
        self.assertFalse(reply.is_error, reply.text)
        self.assertTrue(reply.text.startswith('a{sv}|'), reply.text)



SI_A1 = "dbus.Struct((dbus.String('a'), dbus.Int32(1)), signature=None)"
SI_B2 = "dbus.Struct((dbus.String('b'), dbus.Int32(2)), signature=None)"


class Containers(FixtureTestCase):

    def send(self, method, *values):
        return self.bridge().call('dbus_call', dict(ECHO, method=method, args=list(values)))

    def assertSent(self, method, values, expected):
        self.assertEqual(self.send(method, *values), (expected, False))

    def test_arrays_of_basic_types(self):
        self.assertSent('EchoAU', [[1, 2, 3]],
                        "au|[dbus.Array([dbus.UInt32(1), dbus.UInt32(2), dbus.UInt32(3)], "
                        "signature=dbus.Signature('u'))]")
        self.assertSent('EchoAU', [[]], "au|[dbus.Array([], signature=dbus.Signature('u'))]")
        self.assertSent('EchoAI', [[1, -2, 3]],
                        "ai|[dbus.Array([dbus.Int32(1), dbus.Int32(-2), dbus.Int32(3)], "
                        "signature=dbus.Signature('i'))]")
        self.assertSent('EchoAX', [[-1, 4000000000]],
                        "ax|[dbus.Array([dbus.Int64(-1), dbus.Int64(4000000000)], "
                        "signature=dbus.Signature('x'))]")
        self.assertSent('EchoAT', [[1, '18446744073709551615']],
                        "at|[dbus.Array([dbus.UInt64(1), dbus.UInt64(18446744073709551615)], "
                        "signature=dbus.Signature('t'))]")
        self.assertSent('EchoAD', [[0.5, 2]],
                        "ad|[dbus.Array([dbus.Double(0.5), dbus.Double(2.0)], "
                        "signature=dbus.Signature('d'))]")
        self.assertSent('EchoAB', [[True, False]],
                        "ab|[dbus.Array([dbus.Boolean(True), dbus.Boolean(False)], "
                        "signature=dbus.Signature('b'))]")

    def test_maps(self):
        self.assertSent('EchoASS', [{'k': 'v', 'k2': 'w'}],
                        "a{ss}|[dbus.Dictionary({dbus.String('k'): dbus.String('v'), "
                        "dbus.String('k2'): dbus.String('w')}, signature=dbus.Signature('ss'))]")
        self.assertSent('EchoMapIU', [{'1': 2}],
                        "a{iu}|[dbus.Dictionary({dbus.Int32(1): dbus.UInt32(2)}, "
                        "signature=dbus.Signature('iu'))]")
        self.assertSent('EchoASASV', [{'eth0': {'mtu': 1500, 'up': True}}],
                        "a{sa{sv}}|[dbus.Dictionary({dbus.String('eth0'): dbus.Dictionary("
                        "{dbus.String('mtu'): dbus.Int32(1500, variant_level=1), "
                        "dbus.String('up'): dbus.Boolean(True, variant_level=1)}, "
                        "signature=dbus.Signature('sv'))}, signature=dbus.Signature('sa{sv}'))]")
        self.assertSent('EchoASASS', [{'g': {'k': 'v'}}],
                        "a{sa{ss}}|[dbus.Dictionary({dbus.String('g'): dbus.Dictionary("
                        "{dbus.String('k'): dbus.String('v')}, signature=dbus.Signature('ss'))}, "
                        "signature=dbus.Signature('sa{ss}'))]")

    def test_structs(self):
        self.assertSent('EchoStruct', [['a', 1]], '(si)|[%s]' % SI_A1)
        self.assertSent('EchoASI', [[['a', 1], ['b', 2]]],
                        "a(si)|[dbus.Array([%s, %s], signature=dbus.Signature('(si)'))]"
                        % (SI_A1, SI_B2))
        self.assertSent('EchoASI', [[]], "a(si)|[dbus.Array([], signature=dbus.Signature('(si)'))]")
        self.assertSent('EchoAStructAI', [[[[1, 2]], [[3]]]],
                        "a(ai)|[dbus.Array([dbus.Struct((dbus.Array([dbus.Int32(1), "
                        "dbus.Int32(2)], signature=dbus.Signature('i')),), signature=None), "
                        "dbus.Struct((dbus.Array([dbus.Int32(3)], signature=dbus.Signature('i')),"
                        "), signature=None)], signature=dbus.Signature('(ai)'))]")
        self.assertSent('EchoOX', ['/a/b', 123], "ox|[dbus.ObjectPath('/a/b'), dbus.Int64(123)]")

    def test_nested_arrays(self):
        self.assertSent('EchoAAS', [[['a', 'b'], ['c']]],
                        "aas|[dbus.Array([dbus.Array([dbus.String('a'), dbus.String('b')], "
                        "signature=dbus.Signature('s')), dbus.Array([dbus.String('c')], "
                        "signature=dbus.Signature('s'))], signature=dbus.Signature('as'))]")
        self.assertSent('EchoAAY', [[[104, 105], 'aGk=']],
                        "aay|[dbus.Array([dbus.Array([dbus.Byte(104), dbus.Byte(105)], "
                        "signature=dbus.Signature('y')), dbus.Array([dbus.Byte(104), "
                        "dbus.Byte(105)], signature=dbus.Signature('y'))], "
                        "signature=dbus.Signature('ay'))]")
        self.assertSent('EchoAAI', [[[1, 2], [3]]],
                        "aai|[dbus.Array([dbus.Array([dbus.Int32(1), dbus.Int32(2)], "
                        "signature=dbus.Signature('i')), dbus.Array([dbus.Int32(3)], "
                        "signature=dbus.Signature('i'))], signature=dbus.Signature('ai'))]")
        self.assertSent('EchoAASV', [[{'a': 1}, {'b': 'x'}]],
                        "aa{sv}|[dbus.Array([dbus.Dictionary({dbus.String('a'): "
                        "dbus.Int32(1, variant_level=1)}, signature=dbus.Signature('sv')), "
                        "dbus.Dictionary({dbus.String('b'): dbus.String('x', variant_level=1)}, "
                        "signature=dbus.Signature('sv'))], signature=dbus.Signature('a{sv}'))]")

    def test_several_arguments(self):
        self.assertSent('EchoASAIU', [['a'], [1, 2], 7],
                        "asaiu|[dbus.Array([dbus.String('a')], signature=dbus.Signature('s')), "
                        "dbus.Array([dbus.Int32(1), dbus.Int32(2)], signature=dbus.Signature('i')), "
                        "dbus.UInt32(7)]")

    def test_signature_argument(self):
        self.assertSent('EchoG', ['a{sv}'], "g|[dbus.Signature('a{sv}')]")

    def test_container_rejections(self):
        cases = [
            ('EchoAU', [1, -1], 'argument 0 (au): [1]: -1 out of range [0,4294967295]'),
            ('EchoAU', 5, "argument 0 (au): expected an array for 'au', got 5"),
            ('EchoStruct', ['a'],
             'argument 0 ((si)): expected an array of 2 fields for \'(si)\', got ["a"]'),
            ('EchoASI', [['a', 1], ['b', 'x']],
             'argument 0 (a(si)): [1]: field 1: "x" is not an integer'),
            ('EchoASS', {'k': 1}, "argument 0 (a{ss}): ['k']: expected a string, got 1"),
            ('EchoASS', [1], "argument 0 (a{ss}): expected an object for 'a{ss}', got [1]"),
            ('EchoMapIU', {'x': 1}, 'argument 0 (a{iu}): key \'x\': "x" is not an integer'),
            ('EchoH', 0,
             'argument 0 (h): unix fd arguments are not supported (they cannot travel over MCP)'),
            ('EchoG', 'a{', 'argument 0 (g): "a{" is not a valid D-Bus signature'),
        ]
        for method, value, message in cases:
            with self.subTest(method=method, value=value):
                self.assertEqual(self.send(method, value), (message, True))

    def test_unsupported_element(self):
        self.assertEqual(self.send('EchoUnsupported', []),
                         ("argument 0 (a(sssuda{sv})): unsupported D-Bus signature "
                          "'(sssuda{sv})' (array element)", True))
        self.assertEqual(self.send('EchoMapUnsupported', {}),
                         ("argument 0 (a{s(sssuda{sv})}): unsupported D-Bus signature "
                          "'(sssuda{sv})' (map value)", True))


class Variants(FixtureTestCase):

    def send(self, method, value):
        return self.bridge().call('dbus_call', dict(ECHO, method=method, args=[value]))

    def test_natural_variants(self):
        self.assertEqual(self.send('EchoV', 0.5), ('v|[dbus.Double(0.5, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', 7), ('v|[dbus.Int32(7, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', 5000000000),
                         ('v|[dbus.Int64(5000000000, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', {'k': 1, 'l': ['x']}),
                         ("v|[dbus.Dictionary({dbus.String('k'): dbus.Int32(1, variant_level=1), "
                          "dbus.String('l'): dbus.Array([dbus.String('x', variant_level=1)], "
                          "signature=dbus.Signature('v'), variant_level=1)}, "
                          "signature=dbus.Signature('sv'), variant_level=1)]", False))

    def test_explicit_variant_type(self):
        self.assertEqual(self.send('EchoV', {'@dbus': 'y', 'value': 2}),
                         ('v|[dbus.Byte(2, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', {'@dbus': 'a(si)', 'value': [['a', 1]]}),
                         ("v|[dbus.Array([%s], signature=dbus.Signature('(si)'), "
                          "variant_level=1)]" % SI_A1, False))
        self.assertEqual(self.send('EchoASV', {'n': 1, 's': 'x', 'urg': {'@dbus': 'y', 'value': 2}}),
                         ("a{sv}|[dbus.Dictionary({dbus.String('n'): dbus.Int32(1, variant_level=1), "
                          "dbus.String('s'): dbus.String('x', variant_level=1), "
                          "dbus.String('urg'): dbus.Byte(2, variant_level=1)}, "
                          "signature=dbus.Signature('sv'))]", False))

    def test_variant_inside_a_struct(self):
        reply = self.send('EchoNested', [[['a', 1]], {'@dbus': 'a(ai)', 'value': [[[1]]]}])
        self.assertEqual(reply, (
            "(a(si)v)|[dbus.Struct((dbus.Array([%s], signature=dbus.Signature('(si)')), "
            "dbus.Array([dbus.Struct((dbus.Array([dbus.Int32(1)], signature=dbus.Signature('i')),),"
            " signature=None)], signature=dbus.Signature('(ai)'), variant_level=1)), "
            "signature=None)]" % SI_A1, False))

    def test_dbus_key_has_no_meaning_elsewhere(self):
        reply = self.send('EchoASS', {'@dbus': 'y', 'value': 'x'})
        self.assertFalse(reply.is_error, reply.text)
        self.assertIn("dbus.String('@dbus'): dbus.String('y')", reply.text)

    def test_variant_rejections(self):
        cases = [
            (None, 'argument 0 (v): null cannot be sent in a D-Bus variant'),
            ([1, [2, 'x'], {'k': None}],
             "argument 0 (v): [2]: ['k']: null cannot be sent in a D-Bus variant"),
            ({'@dbus': 'ii', 'value': 1},
             'argument 0 (v): "ii" is not a single complete D-Bus type'),
            ({'@dbus': 'y', 'value': 300}, 'argument 0 (v): 300 out of range [0,255]'),
            ({'@dbus': 'a(sssuda{sv})', 'value': []},
             "argument 0 (v): unsupported D-Bus signature '(sssuda{sv})' (array element)"),
        ]
        for value, message in cases:
            with self.subTest(value=value):
                self.assertEqual(self.send('EchoV', value), (message, True))
        reply = self.send('EchoASV', {'urg': {'@dbus': 'y', 'value': 300}})
        self.assertEqual(reply, ("argument 0 (a{sv}): ['urg']: 300 out of range [0,255]", True))


PROPS_OBJECT = {'service': ECHO['service'], 'path': '/Props'}


class PropertiesSet(FixtureTestCase):

    def set_property(self, prop, value, interface=True):
        bridge = self.bridge()
        arguments = dict(PROPS_OBJECT, method='Set', args=['org.plasmamcp.Props', prop, value])
        if interface:
            arguments['interface'] = 'org.freedesktop.DBus.Properties'
        reply = bridge.call('dbus_call', arguments)
        if reply.is_error:
            return reply
        return bridge.call('dbus_call', dict(PROPS_OBJECT, interface='org.plasmamcp.Props',
                                             method='LastSet'))

    def test_declared_double(self):
        self.assertEqual(self.set_property('Volume', 1),
                         ('ssv|org.plasmamcp.Props.Volume=dbus.Double(1.0, variant_level=1)',
                          False))

    def test_declared_byte(self):
        self.assertEqual(self.set_property('Level', 2),
                         ('ssv|org.plasmamcp.Props.Level=dbus.Byte(2, variant_level=1)', False))

    def test_without_interface(self):
        self.assertEqual(self.set_property('Level', 2, interface=False),
                         ('ssv|org.plasmamcp.Props.Level=dbus.Byte(2, variant_level=1)', False))

    def test_declared_type_is_strict(self):
        self.assertEqual(self.set_property('Level', 300),
                         ('argument 2 (v): 300 out of range [0,255]', True))

    def test_explicit_type_wins(self):
        self.assertEqual(self.set_property('Level', {'@dbus': 'u', 'value': 2}),
                         ('ssv|org.plasmamcp.Props.Level=dbus.UInt32(2, variant_level=1)', False))

    def test_undeclared_property_is_natural(self):
        self.assertEqual(self.set_property('Other', 2),
                         ('ssv|org.plasmamcp.Props.Other=dbus.Int32(2, variant_level=1)', False))


class NaturalMapping(FixtureTestCase):
    """Without introspection data the JSON types decide (m12)."""

    def test_natural_types(self):
        reply = self.bridge().call('dbus_call', {
            'service': ECHO['service'], 'path': '/Loose', 'interface': ECHO['interface'],
            'method': 'EchoAny',
            'args': [7, -2147483648, 2147483648, 5000000000, 2.5, 'x', True, [1, 2], {'k': 1}]})
        self.assertFalse(reply.is_error, reply.text)
        self.assertEqual(reply.text.split('|')[0], 'iixxdsbava{sv}')


def random_signature(rng, depth=0):
    roll = rng.random()
    if depth >= 3 or roll < 0.4:
        return rng.choice('ybnqiuxtdsogvh')
    if roll < 0.6:
        return 'a' + random_signature(rng, depth + 1)
    if roll < 0.75:
        return 'a{' + rng.choice('sybnqiuxtdog') + random_signature(rng, depth + 1) + '}'
    return '(' + ''.join(random_signature(rng, depth + 1)
                         for _ in range(rng.randint(1, 3))) + ')'


def random_json(rng, depth=0):
    roll = rng.random()
    if depth >= 3 or roll < 0.5:
        return rng.choice([0, -1, 300, 2 ** 31, 2 ** 63 - 1, 1.5, '', 'x', '/a', 'aGk=',
                           'a{sv}', True, None])
    if roll < 0.75:
        return [random_json(rng, depth + 1) for _ in range(rng.randint(0, 3))]
    return {rng.choice(['k', '1', 'true', '/p', '@dbus']): random_json(rng, depth + 1)
            for _ in range(rng.randint(0, 3))}


def value_for(rng, sig):
    """A JSON value of the right shape for `sig`, so the build path runs too."""
    c = sig[0]
    if c in 'ynqu':
        return rng.randint(0, 255)
    if c in 'ix':
        return rng.randint(-1000, 1000)
    if c == 't':
        return rng.randint(0, 1000)
    if c == 'b':
        return rng.random() < 0.5
    if c == 'd':
        return rng.random()
    if c in 'sh':
        return 'x'
    if c == 'o':
        return '/a/b'
    if c == 'g':
        return 'a{sv}'
    if c == 'v':
        return rng.choice([1, 'x', [1, 2], {'k': True}])
    if sig.startswith('a{'):
        keys = {'b': 'true', 'd': '1.5', 'o': '/k', 'g': 's', 's': 'k'}
        return {keys.get(sig[2], '1'): value_for(rng, sig[3:-1])
                for _ in range(rng.randint(0, 2))}
    if c == 'a':
        return [value_for(rng, sig[1:]) for _ in range(rng.randint(0, 2))]
    return [value_for(rng, field) for field in dbus.Signature(sig[1:-1])]


class Fuzz(FixtureTestCase):
    """Bounded fuzz (fixed seed): the bridge never dies, every call answers."""

    def test_random_signatures_and_values(self):
        rng = random.Random(20261001)
        session = self.bridge()
        successes = 0
        for n in range(400):
            if rng.random() < 0.1:
                sig = ''.join(rng.choice('ab{}()vsix') for _ in range(rng.randint(1, 6)))
                value = random_json(rng)
            else:
                sig = random_signature(rng)
                value = value_for(rng, sig) if rng.random() < 0.5 else random_json(rng)
            with self.subTest(n=n, sig=sig):
                reply = session.call('dbus_call', dict(ECHO, method='EchoV',
                                                       args=[{'@dbus': sig, 'value': value}]))
                if not reply.is_error:
                    successes += 1
                    self.assertTrue(reply.text.startswith('v|'), reply.text)
        self.assertIsNone(session.proc.poll())
        self.assertGreater(successes, 40)


def nested_list(levels):
    value = 1
    for _ in range(levels):
        value = [value]
    return value


class Limits(FixtureTestCase):
    """D-Bus limits: 32 nested arrays, 32 nested structs (dict entries
    included), 255-character signatures, 64 nested containers in a message
    (variants included). Beyond them libdbus aborts the process or the bus
    drops the connection: the bridge must refuse first and keep answering."""

    def setUp(self):
        self.session = self.bridge()

    def still_connected(self):
        self.assertEqual(self.session.call('dbus_call', dict(ECHO, method='RetU')),
                         ('123456789', False))

    def call(self, method, value, **target):
        arguments = dict(ECHO, method=method, args=[value])
        arguments.update(target)
        return self.session.call('dbus_call', arguments)

    def test_signatures_beyond_the_limits(self):
        deep_struct = '(' * 33 + 'i' + ')' * 33
        deep_array = 'a' * 33 + 'i'
        wide_struct = '(' + 'i' * 300 + ')'
        for sig in (deep_struct, deep_array, wide_struct):
            with self.subTest(sig=sig[:40]):
                reply = self.call('EchoV', {'@dbus': sig, 'value': []})
                self.assertTrue(reply.is_error, reply.text)
                self.assertIn('is not a single complete D-Bus type', reply.text)
                self.still_connected()
        for sig in (deep_struct, deep_array, 'i' * 256):
            with self.subTest(g=sig[:40]):
                reply = self.call('EchoG', sig)
                self.assertTrue(reply.is_error, reply.text)
                self.assertIn('is not a valid D-Bus signature', reply.text)
                self.still_connected()

    def test_signatures_at_the_limits(self):
        reply = self.call('EchoV', {'@dbus': '(' * 32 + 'i' + ')' * 32, 'value': nested_list(32)})
        self.assertFalse(reply.is_error, reply.text)
        reply = self.call('EchoG', '(' * 32 + 'i' + ')' * 32)
        self.assertFalse(reply.is_error, reply.text)

    def test_deep_natural_variant(self):
        reply = self.call('EchoV', nested_list(40))
        self.assertTrue(reply.is_error, reply.text)
        self.assertIn('nested deeper than 64 D-Bus containers', reply.text)
        self.still_connected()
        reply = self.call('EchoV', nested_list(20))
        self.assertFalse(reply.is_error, reply.text)

    def test_deep_untyped_argument(self):
        loose = {'path': '/Loose', 'method': 'EchoAny'}
        reply = self.session.call('dbus_call', dict(ECHO, args=[nested_list(40)], **loose))
        self.assertTrue(reply.is_error, reply.text)
        self.assertIn('nested deeper than 64 D-Bus containers', reply.text)
        self.still_connected()
        reply = self.session.call('dbus_call', dict(ECHO, args=[nested_list(20)], **loose))
        self.assertFalse(reply.is_error, reply.text)


if __name__ == '__main__':
    unittest.main()
