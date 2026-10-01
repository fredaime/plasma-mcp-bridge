# SPDX-License-Identifier: MIT
"""JSON-RPC framing errors and MCP version negotiation."""
import unittest

from mcp_session import FixtureTestCase


class Framing(FixtureTestCase):
    fixtures = ()

    def assertError(self, message, code, rid=None):
        self.assertEqual(message['error']['code'], code, message)
        self.assertEqual(message['id'], rid, message)

    def test_invalid_json(self):
        session = self.bridge()
        session.send_raw('{"jsonrpc": "2.0", "id": 1, "method": "ping"')
        self.assertError(session.read_message(), -32700)

    def test_batches_are_rejected(self):
        session = self.bridge()
        for frame in ('[{"jsonrpc": "2.0", "id": 1, "method": "ping"}]', '[]'):
            with self.subTest(frame=frame):
                session.send_raw(frame)
                self.assertError(session.read_message(), -32600)

    def test_malformed_requests(self):
        session = self.bridge()
        for message, rid in (({'jsonrpc': '2.0', 'id': None, 'method': 'ping'}, None),
                             ({'jsonrpc': '2.0', 'id': 7}, 7),
                             ({'jsonrpc': '2.0', 'id': 'x', 'method': 5}, 'x'),
                             ({'jsonrpc': '2.0', 'id': True, 'method': 'ping'}, None),
                             ({'jsonrpc': '2.0', 'id': {'a': 1}, 'method': 'ping'}, None),
                             ({}, None)):
            with self.subTest(message=message):
                session.send(message)
                self.assertError(session.read_message(), -32600, rid)

    def test_tools_call_params(self):
        session = self.bridge()
        for params in ({}, {'name': 5}, {'name': 'dbus_list_services', 'arguments': [1]},
                       {'name': 'dbus_list_services', 'arguments': 'x'}, 5, [1]):
            with self.subTest(params=params):
                reply = session.request('tools/call', params)
                self.assertEqual(reply['error']['code'], -32602, reply)

    def test_unknown_tool(self):
        reply = self.bridge().request('tools/call', {'name': 'no_such_tool'})
        self.assertEqual(reply['error']['code'], -32602)
        self.assertEqual(reply['error']['message'], 'Unknown tool: no_such_tool')

    def test_unknown_method(self):
        reply = self.bridge().request('server/discover', {})
        self.assertEqual(reply['error']['code'], -32601)

    def test_stray_response_gets_no_reply(self):
        session = self.bridge()
        session.send({'jsonrpc': '2.0', 'id': 42, 'result': {}})
        session.send({'jsonrpc': '2.0', 'id': 43, 'error': {'code': 1, 'message': 'x'}})
        self.assertEqual(session.request('ping')['result'], {})
        self.assertEqual(session.unsolicited, [])
        self.assertIn('ignoring a JSON-RPC response from the client (id 42)',
                      session.stderr_text())

    def test_unknown_notification_is_ignored(self):
        session = self.bridge()
        session.send({'jsonrpc': '2.0', 'method': 'notifications/whatever'})
        self.assertEqual(session.request('ping')['result'], {})
        self.assertEqual(session.unsolicited, [])

    def test_jsonrpc_member_is_optional(self):
        session = self.bridge()
        session.send({'id': 'p', 'method': 'ping'})
        self.assertEqual(session.read_message()['result'], {})

    def test_frame_split_across_writes(self):
        session = self.bridge()
        session.proc.stdin.write(b'{"jsonrpc": "2.0", "id": "s", ')
        session.proc.stdin.flush()
        session.proc.stdin.write(b'"method": "ping"}\n')
        session.proc.stdin.flush()
        self.assertEqual(session.read_message()['id'], 's')

    def test_crlf_line_ending(self):
        session = self.bridge()
        session.proc.stdin.write(b'{"jsonrpc": "2.0", "id": "c", "method": "ping"}\r\n')
        session.proc.stdin.flush()
        self.assertEqual(session.read_message()['id'], 'c')


ABSENT = object()


class Versions(FixtureTestCase):
    fixtures = ()

    def negotiated(self, requested):
        session = self.bridge(initialize=False)
        params = {'capabilities': {}, 'clientInfo': {'name': 'tests', 'version': '0'}}
        if requested is not ABSENT:
            params['protocolVersion'] = requested
        return session.request('initialize', params)['result']['protocolVersion']

    def test_negotiation(self):
        for requested, expected in (('2024-11-05', '2024-11-05'),
                                    ('2025-03-26', '2024-11-05'),
                                    ('2025-06-18', '2025-06-18'),
                                    ('2025-11-25', '2025-11-25'),
                                    ('2026-07-28', '2025-11-25'),
                                    ('2023-01-01', '2025-11-25'),
                                    ('1.0.0', '2025-11-25'),
                                    (5, '2025-11-25'),
                                    (ABSENT, '2025-11-25')):
            with self.subTest(requested=requested):
                self.assertEqual(self.negotiated(requested), expected)


if __name__ == '__main__':
    unittest.main()
