# SPDX-License-Identifier: MIT
"""Tool calls off the main thread: concurrency, plugins, cancellation, timeouts, shutdown."""
import collections
import json
import os
import time
import unittest

from mcp_session import ECHO, FixtureTestCase

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


def tool_call(arguments, name='dbus_call'):
    return {'name': name, 'arguments': arguments}


def interval(message):
    start, end = message['result']['content'][0]['text'].split('-')
    return int(start), int(end)


class Concurrency(FixtureTestCase):

    def test_ping_during_slow_call(self):
        session = self.bridge()
        session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[4])))
        time.sleep(0.2)
        started = time.monotonic()
        self.assertEqual(session.request('ping')['result'], {})
        reply = session.request('server/discover', {})
        self.assertEqual(reply['error']['code'], -32601)
        self.assertLess(time.monotonic() - started, 0.5)

    def test_builtin_calls_run_in_parallel(self):
        session = self.bridge()
        started = time.monotonic()
        for _ in range(2):
            session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[2])))
        replies = session.collect(2, timeout=3.5)
        self.assertLess(time.monotonic() - started, 3.0)
        for reply in replies:
            self.assertEqual(reply['result']['content'][0]['text'], 'slept 2.0s')

    def test_replies_may_come_out_of_order(self):
        session = self.bridge()
        slow = session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[1])))
        fast = session.start('tools/call', tool_call(dict(ECHO, method='RetU')))
        self.assertEqual([m['id'] for m in session.collect(2, timeout=3)], [fast, slow])

    def test_plugin_tools_do_not_overlap(self):
        session = self.bridge('--plugin', TEST_PLUGIN)
        session.start('tools/call', tool_call({'ms': 500}, 'test_sleep_a'))
        session.start('tools/call', tool_call({'ms': 500}, 'test_sleep_b'))
        (a_start, a_end), (b_start, b_end) = sorted(interval(m) for m in session.collect(2, 3))
        self.assertLessEqual(a_end, b_start)

    def test_builtin_tools_run_beside_a_plugin_tool(self):
        session = self.bridge('--plugin', TEST_PLUGIN)
        session.start('tools/call', tool_call({'ms': 1500}, 'test_sleep_a'))
        time.sleep(0.2)
        started = time.monotonic()
        reply = session.call('dbus_call', dict(ECHO, method='RetU'))
        self.assertEqual(reply, ('123456789', False))
        self.assertLess(time.monotonic() - started, 0.5)

    def test_stress(self):
        session = self.bridge(env={'QT_FATAL_WARNINGS': '1'})
        expected = 0
        for i in range(40):
            method, args = ('Slow', [0.05]) if i % 2 else ('RetU', [])
            session.start('tools/call', tool_call(dict(ECHO, method=method, args=args)),
                          rid=1000 + i)
            session.start('ping', rid='ping-%d' % i)
            expected += 2
        seen = collections.Counter()
        for message in session.collect(expected, timeout=15):
            seen[json.dumps(message['id'])] += 1
            if 'result' in message and 'content' in message['result']:
                self.assertFalse(message['result']['isError'], message)
        self.assertEqual(len(seen), expected)
        self.assertEqual(set(seen.values()), {1})
        self.assertIsNone(session.proc.poll())


def cancel(session, rid):
    session.send({'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                  'params': {'requestId': rid, 'reason': 'test'}})


class Cancellation(FixtureTestCase):

    def test_cancelled_call_gets_no_reply(self):
        session = self.bridge()
        rid = session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[1])))
        cancel(session, rid)
        self.assertEqual(session.request('ping')['result'], {})
        session.expect_silence(1.5)
        self.assertNotIn(rid, [m.get('id') for m in session.unsolicited])
        self.assertIn('cancelled request %d' % rid, session.stderr_text())
        self.assertEqual(session.call('dbus_call', dict(ECHO, method='RetU')),
                         ('123456789', False))

    def test_queued_call_is_skipped(self):
        session = self.bridge('--plugin', TEST_PLUGIN)
        session.start('tools/call', tool_call({'ms': 800}, 'test_sleep_a'))
        queued = session.start('tools/call', tool_call({'ms': 0}, 'test_sleep_b'))
        cancel(session, queued)
        first = session.read_message(timeout=2)
        self.assertNotEqual(first['id'], queued)
        session.expect_silence(0.5)
        self.assertIn('test-plugin: test_sleep_a ran', session.stderr_text())
        self.assertNotIn('test-plugin: test_sleep_b ran', session.stderr_text())

    def test_cancel_of_finished_request_is_ignored(self):
        session = self.bridge()
        reply = session.request('tools/call', tool_call(dict(ECHO, method='RetU')))
        cancel(session, reply['id'])
        cancel(session, 999)
        self.assertEqual(session.request('ping')['result'], {})
        self.assertEqual(session.unsolicited, [])
        stderr = session.stderr_text()
        self.assertIn('ignoring cancellation of request %d (unknown or finished)' % reply['id'],
                      stderr)
        self.assertIn('ignoring cancellation of request 999 (unknown or finished)', stderr)

    def test_cancel_of_initialize_is_ignored(self):
        session = self.bridge(initialize=False)
        rid = session.start('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
                                           'clientInfo': {'name': 'tests', 'version': '0'}})
        cancel(session, rid)
        self.assertEqual(session.read_message()['id'], rid)

    def test_duplicate_id_in_flight(self):
        session = self.bridge()
        session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[1])), rid='dup')
        session.start('tools/call', tool_call(dict(ECHO, method='RetU')), rid='dup')
        reply = session.read_message(timeout=2)
        self.assertEqual(reply['id'], 'dup')
        self.assertEqual(reply['result']['content'][0]['text'], 'slept 1.0s')
        session.expect_silence(0.5)
        self.assertIn('ignoring request "dup": a request with this id is still in flight',
                      session.stderr_text())

    def test_ping_with_an_in_flight_id_is_ignored(self):
        session = self.bridge()
        session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[1])), rid=77)
        session.start('ping', rid=77)
        replies = session.collect(1, timeout=2)
        self.assertEqual(replies[0]['result']['content'][0]['text'], 'slept 1.0s')
        session.expect_silence(0.5)

    def test_string_and_number_ids_are_distinct(self):
        session = self.bridge()
        session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[0.5])), rid=1)
        session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[0.5])), rid='1')
        self.assertEqual(sorted(json.dumps(m['id']) for m in session.collect(2, timeout=2)),
                         ['"1"', '1'])


if __name__ == '__main__':
    unittest.main()
