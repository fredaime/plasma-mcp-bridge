# SPDX-License-Identifier: MIT
"""Arguments are converted to the declared D-Bus types, strictly (M6)."""
import unittest

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

    def test_other_containers_still_loose(self):
        # Converted by PR3b; documents the current scope.
        self.assertTrue(self.echo('EchoAU', [1, 2]).text.startswith('av|'))


if __name__ == '__main__':
    unittest.main()
