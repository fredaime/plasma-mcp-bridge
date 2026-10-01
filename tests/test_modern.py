# SPDX-License-Identifier: MIT
"""MCP 2026-07-28 (modern era) beside the historical lifecycle (dual-era)."""
import unittest

from mcp_session import ECHO, FixtureTestCase

MODERN = '2026-07-28'
SERVER_INFO = 'io.modelcontextprotocol/serverInfo'


def meta(version=MODERN, capabilities=None, **extra):
    """The per-request _meta of a 2026-07-28 request."""
    value = {'io.modelcontextprotocol/protocolVersion': version,
             'io.modelcontextprotocol/clientInfo': {'name': 'tests', 'version': '0'},
             'io.modelcontextprotocol/clientCapabilities': {} if capabilities is None
             else capabilities}
    value.update(extra)
    return value


class ModernTestCase(FixtureTestCase):

    def modern(self, method, params=None, session=None, **meta_args):
        """Sends a modern request, by default on a bridge that never saw initialize."""
        session = session or self.bridge(initialize=False)
        body = dict(params or {})
        body['_meta'] = meta(**meta_args)
        return session.request(method, body)

    def assertModernResult(self, reply):
        self.assertNotIn('error', reply, reply)
        result = reply['result']
        self.assertEqual(result['resultType'], 'complete')
        self.assertEqual(result['_meta'][SERVER_INFO]['name'], 'plasma-mcp-bridge')
        return result

    def assertError(self, reply, code):
        self.assertEqual(reply.get('error', {}).get('code'), code, reply)
        return reply['error']


class Discovery(ModernTestCase):
    fixtures = ()

    def test_discover(self):
        result = self.assertModernResult(self.modern('server/discover'))
        self.assertEqual(result['supportedVersions'], [MODERN])
        self.assertEqual(result['capabilities'], {'tools': {'listChanged': False}})
        self.assertEqual(result['ttlMs'], 0)
        self.assertEqual(result['cacheScope'], 'public')
        self.assertEqual(sorted(result), ['_meta', 'cacheScope', 'capabilities', 'resultType',
                                          'supportedVersions', 'ttlMs'])
        self.assertEqual(set(result['_meta'][SERVER_INFO]), {'name', 'version'})

    def test_unsupported_version(self):
        for version in ('1900-01-01', '2025-11-25', '2026-07-29'):
            for method in ('server/discover', 'tools/list', 'ping'):
                with self.subTest(version=version, method=method):
                    error = self.assertError(self.modern(method, version=version), -32022)
                    self.assertEqual(error['data'], {'supported': [MODERN], 'requested': version})

    def test_malformed_meta(self):
        session = self.bridge(initialize=False)
        cases = [
            {'io.modelcontextprotocol/protocolVersion': 5,
             'io.modelcontextprotocol/clientCapabilities': {}},
            {'io.modelcontextprotocol/protocolVersion': MODERN},
            {'io.modelcontextprotocol/protocolVersion': MODERN,
             'io.modelcontextprotocol/clientCapabilities': []},
        ]
        for value in cases:
            with self.subTest(meta=value):
                self.assertError(session.request('server/discover', {'_meta': value}), -32602)

    def test_removed_and_unknown_methods(self):
        for method in ('ping', 'logging/setLevel', 'resources/list', 'prompts/list'):
            with self.subTest(method=method):
                self.assertError(self.modern(method), -32601)

    def test_legacy_requests_unchanged(self):
        session = self.bridge(initialize=False)
        # Without modern _meta, server/discover stays an unknown legacy method
        # and ping keeps working.
        self.assertError(session.request('server/discover', {}), -32601)
        self.assertEqual(session.request('ping')['result'], {})

    def test_initialize_is_always_legacy(self):
        session = self.bridge(initialize=False)
        reply = session.request('initialize', {
            'protocolVersion': MODERN, 'capabilities': {},
            'clientInfo': {'name': 'tests', 'version': '0'}, '_meta': meta()})
        self.assertEqual(reply['result']['protocolVersion'], '2025-11-25')
        self.assertEqual(reply['result']['serverInfo']['name'], 'plasma-mcp-bridge')
        self.assertNotIn('resultType', reply['result'])


if __name__ == '__main__':
    unittest.main()
