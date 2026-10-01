# SPDX-License-Identifier: MIT
"""Reply demarshalling: regressions, B1 (nested string/byte arrays)."""
import unittest

from mcp_session import ECHO, FixtureTestCase


class Demarshall(FixtureTestCase):

    def ret(self, method, timeout=2.0):
        return self.bridge().call_json('dbus_call', dict(ECHO, method=method), timeout=timeout)

    # --- unchanged behaviour ---------------------------------------------
    def test_top_level_byte_array_is_base64(self):
        reply = self.bridge().call('dbus_call', dict(ECHO, method='RetAY'))
        self.assertEqual(reply, ('aGVsbG8=', False))

    def test_map_of_variants(self):
        self.assertEqual(self.ret('RetASV'),
                         {'pos': 123456789, 'pi': 3.14159265, 'pid': 3303203})

    def test_array_of_structs(self):
        self.assertEqual(self.ret('RetAStructISS'), [[1, 'a', 'b'], [2, 'c', 'd']])

    def test_nested_int_arrays(self):
        self.assertEqual(self.ret('RetAAI'), [[1, 2], [3]])
        self.assertEqual(self.ret('RetAStructAI'), [[[1, 2]]])

    def test_object_path_and_signature_arrays(self):
        self.assertEqual(self.ret('RetStructAO'), [['/a']])
        self.assertEqual(self.ret('RetAO'), ['/a', '/b/c'])
        self.assertEqual(self.ret('RetAG'), ['i', 'a{sv}'])

    def test_variant_holding_string_array(self):
        self.assertEqual(self.ret('RetAVofAS'), [['a']])

    def test_empty_containers(self):
        self.assertEqual(self.ret('RetEmptyAS'), [])
        self.assertEqual(self.ret('RetEmptyMapAS'), {})

    def test_empty_top_level_byte_array(self):
        reply = self.bridge().call('dbus_call', dict(ECHO, method='RetEmptyAY'))
        self.assertEqual(reply, ('', False))

    def test_big_map(self):
        reply = self.ret('RetBigASV', timeout=5.0)
        self.assertEqual(len(reply), 5000)
        self.assertEqual(reply['k4999'], 4999)

    # --- B1 ---------------------------------------------------------------
    def test_array_of_string_arrays(self):
        self.assertEqual(self.ret('RetAAS'), [['a', 'b'], ['c']])

    def test_aas_with_empty_inner(self):
        self.assertEqual(self.ret('RetAASWithEmpty'), [[], ['x']])

    def test_struct_of_string_array(self):
        self.assertEqual(self.ret('RetStructAS'), [['a', 'b']])

    def test_array_of_byte_arrays(self):
        self.assertEqual(self.ret('RetAAY'), ['aGk='])

    def test_map_of_string_arrays(self):
        self.assertEqual(self.ret('RetMapAS'), {'k': ['a']})

    def test_map_of_byte_arrays(self):
        self.assertEqual(self.ret('RetMapAY'), {'k': 'aGk='})

    # --- M7: non-string map keys -----------------------------------------
    def test_map_object_path_key(self):
        self.assertEqual(self.ret('RetMapOAS'), {'/p': ['x']})

    def test_map_signature_key(self):
        self.assertEqual(self.ret('RetMapGAS'), {'a{sv}': ['x']})

    def test_map_int_key(self):
        self.assertEqual(self.ret('RetMapIS'), {'1': 'a', '2': 'b'})

    def test_map_bool_key(self):
        self.assertEqual(self.ret('RetMapBS'), {'true': 'y', 'false': 'n'})

    def test_map_byte_key_is_a_number(self):
        self.assertEqual(self.ret('RetMapYS'), {'0': 'a', '65': 'b', '200': 'c'})


if __name__ == '__main__':
    unittest.main()
