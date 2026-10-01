# SPDX-License-Identifier: MIT
"""Test-suite helpers: MCP stdio client, D-Bus fixture launcher, base TestCase.

Every process started here gets PR_SET_PDEATHSIG, so a killed test never
leaves a bridge or fixture behind. Everything refuses to run unless the
private bus of tests/run_with_bus.sh is active.
"""
import collections
import ctypes
import json
import os
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ECHO = {'service': 'org.plasmamcp.Validation', 'path': '/Echo',
        'interface': 'org.plasmamcp.Validation'}

_PR_SET_PDEATHSIG = 1
_libc = ctypes.CDLL('libc.so.6', use_errno=True)


def _die_with_parent():
    _libc.prctl(_PR_SET_PDEATHSIG, signal.SIGKILL)


def require_private_bus():
    marker = os.environ.get('PLASMA_MCP_TEST_BUS')
    if not marker or marker != os.environ.get('DBUS_SESSION_BUS_ADDRESS'):
        raise RuntimeError('refusing to run outside the private test bus: '
                           'use tests/run_with_bus.sh (or ctest)')


class MCPTimeout(AssertionError):
    """No reply within the deadline. The bridge has been killed."""


class MCPError(Exception):
    def __init__(self, error):
        super().__init__(error.get('message', error))
        self.error = error


ToolReply = collections.namedtuple('ToolReply', 'text is_error')


class _LineReader:
    """Reads newline-terminated frames from a pipe with a deadline."""

    def __init__(self, fd):
        self._fd = fd
        self._buf = b''

    def readline(self, deadline):
        while b'\n' not in self._buf:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError
            ready, _, _ = select.select([self._fd], [], [], remaining)
            if not ready:
                continue
            chunk = os.read(self._fd, 65536)
            if not chunk:
                raise EOFError('pipe closed')
            self._buf += chunk
        line, _, self._buf = self._buf.partition(b'\n')
        return line


class MCPSession:
    def __init__(self, *extra_args, initialize=True, protocol_version='2024-11-05', env=None):
        require_private_bus()
        self._stderr = tempfile.TemporaryFile()
        self.proc = subprocess.Popen(
            [os.environ['PLASMA_MCP_BRIDGE'], *extra_args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._stderr,
            env=dict(os.environ, **(env or {})), preexec_fn=_die_with_parent)
        self._reader = _LineReader(self.proc.stdout.fileno())
        self._next_id = 1
        self.unsolicited = []
        if initialize:
            self.init_result = self.request('initialize', {
                'protocolVersion': protocol_version, 'capabilities': {},
                'clientInfo': {'name': 'plasma-mcp-bridge-tests', 'version': '0'}})['result']
            self.send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})

    # --- raw I/O ------------------------------------------------------------
    def send(self, obj):
        self.send_raw(json.dumps(obj))

    def send_raw(self, line):
        self.proc.stdin.write(line.encode() + b'\n')
        self.proc.stdin.flush()

    def read_message(self, timeout=2.0):
        try:
            line = self._reader.readline(time.monotonic() + timeout)
        except TimeoutError:
            self.kill()
            raise MCPTimeout('no message from the bridge within %.1fs' % timeout) from None
        return json.loads(line)

    # --- requests -----------------------------------------------------------
    def request(self, method, params=None, timeout=2.0):
        rid = self._next_id
        self._next_id += 1
        message = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            message['params'] = params
        self.send(message)
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.kill()
                raise MCPTimeout('no reply to %s id=%d within %.1fs' % (method, rid, timeout))
            reply = self.read_message(remaining)
            if reply.get('id') == rid:
                return reply
            self.unsolicited.append(reply)

    def call(self, tool, arguments=None, timeout=2.0):
        reply = self.request('tools/call', {'name': tool, 'arguments': arguments or {}}, timeout)
        if 'error' in reply:
            raise MCPError(reply['error'])
        result = reply['result']
        return ToolReply(result['content'][0]['text'], bool(result.get('isError')))

    def call_json(self, tool, arguments=None, timeout=2.0):
        reply = self.call(tool, arguments, timeout)
        if reply.is_error:
            raise AssertionError('tool error: ' + reply.text)
        return json.loads(reply.text)

    def start(self, method, params=None, rid=None):
        """Sends a request without waiting for its reply; returns its id."""
        if rid is None:
            rid = self._next_id
            self._next_id += 1
        message = {'jsonrpc': '2.0', 'id': rid, 'method': method}
        if params is not None:
            message['params'] = params
        self.send(message)
        return rid

    def collect(self, count, timeout=2.0):
        """Reads `count` messages within `timeout`; returns them in arrival order."""
        deadline = time.monotonic() + timeout
        return [self.read_message(max(0.0, deadline - time.monotonic())) for _ in range(count)]

    def expect_silence(self, seconds):
        """Fails if anything arrives on stdout within `seconds`."""
        try:
            line = self._reader.readline(time.monotonic() + seconds)
        except TimeoutError:
            return
        raise AssertionError('unexpected message: %s' % line.decode(errors='replace'))

    # --- lifecycle ----------------------------------------------------------
    def close(self, timeout=2.0):
        if self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.kill()
                raise MCPTimeout('bridge did not exit within %.1fs after stdin EOF' % timeout)
        self._release()
        return self.proc.returncode

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self._release()

    def _release(self):
        for stream in (self.proc.stdin, self.proc.stdout):
            if not stream.closed:
                stream.close()

    def stderr_text(self):
        self._stderr.seek(0)
        return self._stderr.read().decode(errors='replace')

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.kill()

    def __del__(self):
        self._stderr.close()


class DBusFixture:
    """Starts a fixture script and waits for its 'READY <bus-name>' line."""

    def __init__(self, script, *args, timeout=5.0):
        require_private_bus()
        self.proc = subprocess.Popen([sys.executable, script, *args],
                                     stdout=subprocess.PIPE, preexec_fn=_die_with_parent)
        reader = _LineReader(self.proc.stdout.fileno())
        try:
            line = reader.readline(time.monotonic() + timeout).decode()
        except (TimeoutError, EOFError):
            self.stop()
            raise RuntimeError('fixture %s did not report READY within %.1fs'
                               % (os.path.basename(script), timeout)) from None
        if not line.startswith('READY '):
            self.stop()
            raise RuntimeError('fixture %s printed %r instead of READY' % (script, line))
        self.name = line.split(' ', 1)[1].strip()

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        if not self.proc.stdout.closed:
            self.proc.stdout.close()


class FixtureTestCase(unittest.TestCase):
    """Starts `fixtures` once per test class; one fresh bridge per test."""

    fixtures = ('fixtures/echo_service.py',)

    @classmethod
    def setUpClass(cls):
        cls._running = []
        for script in cls.fixtures:
            cls._running.append(DBusFixture(os.path.join(HERE, script)))

    @classmethod
    def tearDownClass(cls):
        for fixture in cls._running:
            fixture.stop()

    def bridge(self, *extra_args, **kw):
        session = MCPSession(*extra_args, **kw)
        self.addCleanup(session.kill)
        return session
