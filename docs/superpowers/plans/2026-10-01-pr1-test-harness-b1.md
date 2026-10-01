# PR1 — Socle de tests + hotfix B1 + M7 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Donner au core une suite de tests sur bus D-Bus privé, puis corriger la boucle infinie du démarshalleur (B1) et la perte des clés de map `o`/`g` (M7).

**Architecture:** Tests Python `unittest` lancés par CTest via `tests/run_with_bus.sh` (bus de session jetable sans services activables, bus système redirigé dessus). Un oracle D-Bus (`tests/fixtures/echo_service.py`) renvoie des valeurs typées ; un client MCP (`tests/mcp_session.py`) pilote le bridge avec un délai par appel. Le correctif B1/M7 est confiné à `src/dbus/dbusbridge.cpp`.

**Tech Stack:** C++17, Qt 6 Core + DBus (≥ 6.4), CMake ≥ 3.16 + ECM, Python 3 stdlib + python3-dbus + python3-gi, `dbus-run-session` (paquet `dbus-daemon`).

**Spec:** `docs/superpowers/specs/2026-10-01-remediation-core-design.md` (§3 socle, §4.1 A0, §9 changelog, §10 PR1).

## Global Constraints

- Seule dépendance dure : Qt 6 Core + DBus. Aucune nouvelle dépendance C++.
- Doit compiler en `-Wall -Wextra -Werror` sur Qt 6.4.2 (CI `ubuntu-24.04`) **et** Qt 6.11 (poste local).
- Aucun en-tête installé ne change de structure ; IID `org.kde.plasma.mcpbridge.PluginInterface/1.0` inchangé.
- stdout du bridge = protocole uniquement ; logs sur stderr.
- Chaque nouveau fichier source porte `// SPDX-License-Identifier: MIT` (C++/CMake) ou `# SPDX-License-Identifier: MIT` (Python/shell).
- Tests : `unittest` stdlib, jamais sur le bus de l'utilisateur, `TIMEOUT 120` par test CTest, assertions sur les **noms** d'erreur D-Bus, jamais sur leurs messages.
- Un dépassement de délai dans un test est un **échec** qui tue le bridge, jamais une attente infinie.
- Messages de commit terminés par la ligne `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Travailler sur une branche `remediation/pr1-harness-b1` créée depuis `main` (dans un worktree, cf. superpowers:using-git-worktrees). Ne jamais committer sur `main`.

## Review Focus

- **Réponse volumineuse** (des milliers d'entrées dans un `a{sv}`) : le démarshalleur réécrit doit tout décoder sans tronquer ni ralentir → test `test_big_map` (Task 3).
- **Clés de map non-chaîne** (`a{is}`, `a{bs}`) : un client attend des clés JSON `"1"`, `"true"`, pas `""` → test `test_map_int_key` (Task 4).
- **Conteneurs vides imbriqués** (`aas` contenant `[]`) : on attend `[[], ["x"]]`, pas un marqueur ni une troncature → test `test_aas_with_empty_inner` (Task 3).
- **Fixture qui ne démarre pas** (dépendance Python manquante, crash) : le test doit échouer vite avec un message clair, pas pendre → test `test_fixture_never_ready` (Task 2).
- **Tests lancés hors du lanceur** (`python3 -m unittest` sur le bus réel) : refus explicite avant de démarrer quoi que ce soit → test `test_refuses_outside_private_bus` (Task 2).

---

## File Structure

| Fichier | Responsabilité |
|---|---|
| `CMakeLists.txt` (modifié) | Macros de dépréciation ; `add_subdirectory(tests)` derrière `BUILD_TESTING` ; version `0.1.90`. |
| `tests/CMakeLists.txt` (créé) | Détection tolérante des dépendances ; un `add_test` par module Python. |
| `tests/run_with_bus.sh` (créé, 755) | Lance une commande sur un bus privé jetable, pose l'environnement de test. |
| `tests/fixtures/session.conf` (créé) | Configuration du bus privé, sans `servicedir`. |
| `tests/fixtures/echo_service.py` (créé) | Oracle D-Bus `org.plasmamcp.Validation` à `/Echo`. |
| `tests/fixtures/never_ready.py` (créé) | Fixture factice qui n'annonce jamais `READY` (test du harnais). |
| `tests/mcp_session.py` (créé) | Client MCP, lanceur de fixtures, classe de base des tests. |
| `tests/test_smoke.py` (créé) | Protocole de base sur bus privé. |
| `tests/test_harness.py` (créé) | Garde-fous du harnais lui-même. |
| `tests/test_demarshall.py` (créé) | Démarshalling : non-régression, B1, M7. |
| `src/dbus/dbusbridge.cpp` (modifié) | `extractBasic`, `demarshallElement`, `mapKeyToString`, `demarshall`. |
| `.github/workflows/build.yml` (modifié) | Dépendances de test + étape `ctest`. |
| `.gitignore` (modifié) | `__pycache__/`. |
| `CHANGELOG.md` (créé) | Section « 0.2.0 (non publiée) ». |
| `README.md`, `CLAUDE.md` (modifiés) | Comment lancer les tests ; conventions. |

---

### Task 1: Harnais de tests et test de fumée

**Files:**
- Modify: `CMakeLists.txt` (après `find_package(Qt6 …)`, autour des lignes 20-23)
- Modify: `.gitignore`
- Create: `tests/CMakeLists.txt`, `tests/run_with_bus.sh`, `tests/fixtures/session.conf`, `tests/fixtures/echo_service.py`, `tests/mcp_session.py`, `tests/test_smoke.py`

**Interfaces:**
- Consumes: le binaire `plasma-mcp-bridge` (cible CMake existante).
- Produces (utilisés par toutes les PR suivantes) :
  - variables d'environnement posées par `run_with_bus.sh` : `PLASMA_MCP_TEST_BUS` (= adresse du bus privé), `DBUS_SYSTEM_BUS_ADDRESS` (= même adresse), `LANG=LC_ALL=C.UTF-8` ; posée par CTest : `PLASMA_MCP_BRIDGE` (chemin du binaire).
  - `mcp_session.require_private_bus() -> None` (lève `RuntimeError`).
  - `mcp_session.MCPTimeout(AssertionError)`, `mcp_session.MCPError(Exception)` (attribut `.error: dict`).
  - `mcp_session.ToolReply(text: str, is_error: bool)` (namedtuple).
  - `mcp_session.MCPSession(*extra_args, initialize=True, protocol_version='2024-11-05')` : `.send(obj)`, `.send_raw(line: str)`, `.read_message(timeout) -> dict`, `.request(method, params=None, timeout=2.0) -> dict`, `.call(tool, arguments=None, timeout=2.0) -> ToolReply`, `.call_json(tool, arguments=None, timeout=2.0) -> object`, `.close(timeout=2.0) -> int`, `.kill()`, `.stderr_text() -> str`, `.proc`, gestionnaire de contexte.
  - `mcp_session.DBusFixture(script, *args, timeout=5.0)` : `.name: str`, `.stop()`.
  - `mcp_session.FixtureTestCase(unittest.TestCase)` : attribut de classe `fixtures = ('fixtures/echo_service.py',)`, méthode `self.bridge(*extra_args, **kw) -> MCPSession`.
  - `mcp_session.ECHO = {'service': 'org.plasmamcp.Validation', 'path': '/Echo', 'interface': 'org.plasmamcp.Validation'}`.
  - Convention fixtures : un script de fixture imprime `READY <nom-bus>` sur stdout une fois son nom acquis, et refuse de démarrer si `PLASMA_MCP_TEST_BUS != DBUS_SESSION_BUS_ADDRESS`.
  - `tests/CMakeLists.txt` : liste `_tests` avec **un module par ligne** (les PR suivantes y ajoutent une ligne chacune).

- [ ] **Step 1: Créer la branche de travail**

```bash
git switch main && git switch -c remediation/pr1-harness-b1
```

- [ ] **Step 2: Brancher les tests et les macros de dépréciation dans `CMakeLists.txt`**

Remplacer le bloc :

```cmake
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core DBus)

add_subdirectory(src)
add_subdirectory(data)
```

par :

```cmake
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core DBus)

# Same deprecation warnings on the CI's Qt 6.4 and on newer local Qt: APIs
# deprecated after 6.4 do not warn (QT_WARN_DEPRECATED_UP_TO is the 6.5+ name,
# QT_DEPRECATED_WARNINGS_SINCE the 6.4 one).
add_compile_definitions(QT_WARN_DEPRECATED_UP_TO=0x060400 QT_DEPRECATED_WARNINGS_SINCE=0x060400)

add_subdirectory(src)
add_subdirectory(data)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

- [ ] **Step 3: Ignorer les caches Python** — ajouter à la fin de `.gitignore` :

```
__pycache__/
```

- [ ] **Step 4: Créer `tests/fixtures/session.conf`**

```xml
<!-- SPDX-License-Identifier: MIT -->
<!-- Throw-away session bus for the test-suite. Deliberately NO servicedir:
     nothing can be D-Bus-activated, so a test can only reach the fixtures it
     started itself. -->
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
```

(Le commentaire SPDX est placé avant le DOCTYPE : un commentaire XML y est autorisé.)

- [ ] **Step 5: Créer `tests/run_with_bus.sh` puis le rendre exécutable**

```sh
#!/bin/sh
# SPDX-License-Identifier: MIT
# Run a command on a throw-away private session bus.
#   tests/run_with_bus.sh python3 -m unittest -v test_smoke
# The system bus is redirected to the same private bus, so bus:"system" in a
# test can never reach the real system bus.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
exec dbus-run-session --config-file="$here/fixtures/session.conf" -- sh -c '
  export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
  export PLASMA_MCP_TEST_BUS="$DBUS_SESSION_BUS_ADDRESS"
  export LANG=C.UTF-8 LC_ALL=C.UTF-8
  exec "$@"' sh "$@"
```

```bash
chmod 755 tests/run_with_bus.sh
```

- [ ] **Step 6: Créer `tests/fixtures/echo_service.py`**

```python
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
    def decorator(fn):
        if signature == 'ox':
            def method(self, a, b, msg=None):
                return '%s|%r' % (msg.get_signature(), [a, b])
        else:
            def method(self, a, msg=None):
                return '%s|%r' % (msg.get_signature(), [a])
        method.__name__ = fn.__name__
        return dbus.service.method(IFACE, in_signature=signature, out_signature='s',
                                   message_keyword='msg')(method)
    return decorator


class Echo(dbus.service.Object):
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

    # --- Ret: typed replies -------------------------------------------------
    @dbus.service.method(IFACE, in_signature='', out_signature='u')
    def RetU(self): return dbus.UInt32(123456789)
    @dbus.service.method(IFACE, in_signature='', out_signature='x')
    def RetX(self): return dbus.Int64(4000000)
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
    # M7: non-string map keys
    @dbus.service.method(IFACE, in_signature='', out_signature='a{oas}')
    def RetMapOAS(self): return {dbus.ObjectPath('/p'): ['x']}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{gas}')
    def RetMapGAS(self): return {dbus.Signature('a{sv}'): ['x']}
    @dbus.service.method(IFACE, in_signature='', out_signature='a{is}')
    def RetMapIS(self): return {1: 'a', 2: 'b'}

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


def main():
    if os.environ.get('PLASMA_MCP_TEST_BUS') != os.environ.get('DBUS_SESSION_BUS_ADDRESS'):
        sys.exit('echo_service: refusing to own names outside the private test bus '
                 '(run through tests/run_with_bus.sh)')
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    name = dbus.service.BusName(NAME, bus, do_not_queue=True)  # noqa: F841 (keeps the name)
    Echo(bus, '/Echo')
    print('READY ' + NAME, flush=True)
    GLib.MainLoop().run()


if __name__ == '__main__':
    main()
```

- [ ] **Step 7: Créer `tests/mcp_session.py`**

```python
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
    def __init__(self, *extra_args, initialize=True, protocol_version='2024-11-05'):
        require_private_bus()
        self._stderr = tempfile.TemporaryFile()
        self.proc = subprocess.Popen(
            [os.environ['PLASMA_MCP_BRIDGE'], *extra_args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._stderr,
            preexec_fn=_die_with_parent)
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

    # --- lifecycle ----------------------------------------------------------
    def close(self, timeout=2.0):
        if self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout)
            except subprocess.TimeoutExpired:
                self.kill()
                raise MCPTimeout('bridge did not exit within %.1fs after stdin EOF' % timeout)
        return self.proc.returncode

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()

    def stderr_text(self):
        self._stderr.seek(0)
        return self._stderr.read().decode(errors='replace')

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.kill()


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
```

- [ ] **Step 8: Créer `tests/test_smoke.py`**

```python
# SPDX-License-Identifier: MIT
"""Protocol basics on the private bus."""
import unittest

from mcp_session import FixtureTestCase


class Smoke(FixtureTestCase):
    fixtures = ()

    def test_initialize(self):
        result = self.bridge().init_result
        self.assertEqual(result['serverInfo']['name'], 'plasma-mcp-bridge')
        self.assertIn('tools', result['capabilities'])
        self.assertEqual(result['protocolVersion'], '2024-11-05')

    def test_tools_list(self):
        tools = self.bridge().request('tools/list')['result']['tools']
        self.assertEqual([t['name'] for t in tools],
                         ['dbus_list_services', 'dbus_introspect', 'dbus_call', 'desktop_notify'])

    def test_ping(self):
        self.assertEqual(self.bridge().request('ping')['result'], {})

    def test_eof_exits_cleanly(self):
        self.assertEqual(self.bridge().close(timeout=2.0), 0)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 9: Créer `tests/CMakeLists.txt`**

```cmake
# SPDX-License-Identifier: MIT
# Python tests on a private D-Bus. Missing test dependencies only disable the
# tests (WARNING), so consumers that configure the core without them still build.
find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python 3 not found: plasma-mcp-bridge tests disabled")
    return()
endif()

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import dbus, dbus.service, dbus.mainloop.glib; from gi.repository import GLib"
    RESULT_VARIABLE _plasma_mcp_test_deps OUTPUT_QUIET ERROR_QUIET)
if(NOT _plasma_mcp_test_deps EQUAL 0)
    message(WARNING "python3-dbus or python3-gi not found: plasma-mcp-bridge tests disabled")
    return()
endif()

find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)
if(NOT DBUS_RUN_SESSION_EXECUTABLE)
    message(WARNING "dbus-run-session not found: plasma-mcp-bridge tests disabled")
    return()
endif()

# One module per line: each PR adds its own line.
set(_tests
    test_smoke
)

foreach(_test IN LISTS _tests)
    add_test(NAME ${_test}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_with_bus.sh
                ${Python3_EXECUTABLE} -m unittest -v ${_test}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${_test} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "PLASMA_MCP_BRIDGE=$<TARGET_FILE:plasma-mcp-bridge>;PYTHONDONTWRITEBYTECODE=1")
endforeach()
```

- [ ] **Step 10: Configurer, compiler, lancer**

Run:
```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" \
  && cmake --build build && ctest --test-dir build --output-on-failure -R test_smoke
```
Expected: build sans warning ; `test_smoke` PASS (4 tests). Si le build échoue à cause d'une dépréciation, remplacer l'appel signalé par son équivalent non déprécié disponible en Qt 6.4 (ne pas retirer `-Werror`).

- [ ] **Step 11: Commit**

```bash
git add CMakeLists.txt .gitignore tests/
git commit -m "test: private-bus test harness and protocol smoke test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Garde-fous du harnais

**Files:**
- Create: `tests/test_harness.py`, `tests/fixtures/never_ready.py`
- Modify: `tests/CMakeLists.txt` (liste `_tests`)

**Interfaces:**
- Consumes: tout ce que produit Task 1.
- Produces: rien de nouveau ; verrouille le comportement du harnais.

- [ ] **Step 1: Créer `tests/fixtures/never_ready.py`**

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fixture that never reports READY (exercises the harness timeout)."""
import time

time.sleep(60)
```

- [ ] **Step 2: Écrire `tests/test_harness.py`**

```python
# SPDX-License-Identifier: MIT
"""The harness must protect the user's buses and never hang."""
import os
import subprocess
import sys
import threading
import time
import unittest

import dbus

from mcp_session import ECHO, HERE, DBusFixture, FixtureTestCase, MCPTimeout


class Harness(FixtureTestCase):

    def test_refuses_outside_private_bus(self):
        env = dict(os.environ)
        env.pop('PLASMA_MCP_TEST_BUS')
        probe = subprocess.run(
            [sys.executable, '-c', 'import mcp_session; mcp_session.MCPSession()'],
            cwd=HERE, env=env, capture_output=True, text=True, timeout=10)
        self.assertNotEqual(probe.returncode, 0)
        self.assertIn('refusing to run outside the private test bus', probe.stderr)

    def test_system_bus_is_the_private_bus(self):
        names = self.bridge().call_json('dbus_list_services', {'bus': 'system'})
        self.assertIn('org.plasmamcp.Validation', names)
        self.assertNotIn('org.freedesktop.login1', names)

    def test_nothing_is_activatable(self):
        names = self.bridge().call_json('dbus_call', {
            'service': 'org.freedesktop.DBus', 'path': '/org/freedesktop/DBus',
            'method': 'ListActivatableNames'})
        self.assertEqual(names, ['org.freedesktop.DBus'])

    def test_timeout_kills_the_bridge(self):
        session = self.bridge()
        started = time.monotonic()
        with self.assertRaises(MCPTimeout):
            session.call('dbus_call', dict(ECHO, method='Slow', args=[5]), timeout=0.5)
        self.assertLess(time.monotonic() - started, 2.0)
        self.assertIsNotNone(session.proc.poll())

    def test_fixture_never_ready(self):
        started = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, 'did not report READY'):
            DBusFixture(os.path.join(HERE, 'fixtures/never_ready.py'), timeout=1.0)
        self.assertLess(time.monotonic() - started, 4.0)

    def test_oracle_slow_overlaps(self):
        # Slow() must not serialise the service, or concurrency tests would
        # measure the oracle instead of the bridge.
        def one():
            proxy = dbus.SessionBus(private=True).get_object(ECHO['service'], ECHO['path'])
            proxy.Slow(1.0, dbus_interface=ECHO['interface'])
        started = time.monotonic()
        threads = [threading.Thread(target=one) for _ in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(5)
        self.assertLess(time.monotonic() - started, 1.8)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 3: Ajouter la ligne `test_harness` à la liste `_tests` de `tests/CMakeLists.txt`**

```cmake
set(_tests
    test_smoke
    test_harness
)
```

- [ ] **Step 4: Lancer**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R test_harness`
Expected: PASS (6 tests). Puis vérifier l'absence d'orphelins : `pgrep -af 'plasma-mcp-bridge|echo_service|never_ready'` → aucune sortie.

- [ ] **Step 5: Commit**

```bash
git add tests/
git commit -m "test: harness guards (private bus only, no activation, bounded timeouts)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Hotfix B1 — fin de la boucle infinie du démarshalleur

**Files:**
- Create: `tests/test_demarshall.py`
- Modify: `tests/CMakeLists.txt` (liste `_tests`)
- Modify: `src/dbus/dbusbridge.cpp:257-325` (de `// Extract the current basic-typed element` jusqu'à la fin de `DBusBridge::demarshall`)

**Interfaces:**
- Consumes: `FixtureTestCase`, `ECHO`, `MCPSession.call_json`, `MCPSession.call` (Task 1).
- Produces: dans `dbusbridge.cpp`, fonctions internes `static QVariant extractBasic(const QDBusArgument &)`, `static QJsonValue unsupportedElement(const QString &signature)`, `static bool demarshallElement(const QDBusArgument &, QJsonValue &out)` ; la signature publique `QJsonValue DBusBridge::demarshall(const QDBusArgument &)` est inchangée. Marqueur de type non lisible : `"<unsupported D-Bus type '<sig>'>"`.

- [ ] **Step 1: Écrire `tests/test_demarshall.py` (non-régression + B1)**

```python
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


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Ajouter `test_demarshall` à la liste `_tests`**

```cmake
set(_tests
    test_smoke
    test_harness
    test_demarshall
)
```

- [ ] **Step 3: Vérifier l'échec**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R test_demarshall`
Expected: FAIL. `test_array_of_string_arrays`, `test_aas_with_empty_inner`, `test_struct_of_string_array`, `test_array_of_byte_arrays` échouent par `MCPTimeout` (≈ 2 s chacun, bridge tué) ; `test_map_of_string_arrays` et `test_map_of_byte_arrays` échouent sur `{'k': ''}`. Les autres passent.

- [ ] **Step 4: Remplacer le démarshalleur dans `src/dbus/dbusbridge.cpp`**

Remplacer tout le bloc qui commence au commentaire `// Extract the current basic-typed element into a properly typed lvalue.` et se termine à la fin de `QJsonValue DBusBridge::demarshall(...)` (fin du fichier) par :

```cpp
// Extract the current basic-typed element into a properly typed lvalue.
// Extracting into a QVariant (operator>>(..., QVariant&)) is NOT equivalent:
// that overload assumes the current element is a D-Bus variant ('v') and
// recurses into it unconditionally — on any other element type the libdbus
// iterator recursion crashes. So dispatch on the wire signature instead.
//
// Returns an invalid QVariant for a wire type we do not know how to read. In
// that case NOTHING was consumed: every QDBusArgument::operator>> is
// type-checked and, on a mismatch, returns a default value without advancing
// the iterator. A blind read here would leave the caller's atEnd() loop
// spinning forever.
static QVariant extractBasic(const QDBusArgument &argument)
{
    const QString signature = argument.currentSignature();
    switch (signature.isEmpty() ? '\0' : signature.at(0).toLatin1()) {
    case 'b': { bool v; argument >> v; return v; }
    case 'y': { uchar v; argument >> v; return v; }
    case 'n': { short v; argument >> v; return v; }
    case 'q': { ushort v; argument >> v; return v; }
    case 'i': { int v; argument >> v; return v; }
    case 'u': { uint v; argument >> v; return v; }
    case 'x': { qlonglong v; argument >> v; return v; }
    case 't': { qulonglong v; argument >> v; return v; }
    case 'd': { double v; argument >> v; return v; }
    case 's': { QString v; argument >> v; return v; }
    case 'o': { QDBusObjectPath v; argument >> v; return QVariant::fromValue(v); }
    case 'g': { QDBusSignature v; argument >> v; return QVariant::fromValue(v); }
    case 'h': { QDBusUnixFileDescriptor v; argument >> v; return QVariant::fromValue(v); }
    case 'a':
        // QDBusDemarshaller::currentType() reports byte and string arrays as
        // BasicType (they map to QByteArray / QStringList), so 'ay' and 'as'
        // land here rather than in the ArrayType branch of demarshall.
        if (signature == QLatin1String("ay")) { QByteArray v; argument >> v; return v; }
        if (signature == QLatin1String("as")) { QStringList v; argument >> v; return v; }
        return QVariant();
    default:
        return QVariant();
    }
}

static QJsonValue unsupportedElement(const QString &signature)
{
    return QJsonValue(QStringLiteral("<unsupported D-Bus type '%1'>").arg(signature));
}

// Demarshall the current element into `out`. Returns false when the element
// was NOT consumed (its wire type is unknown to extractBasic): `out` then
// holds an explanatory marker and the enclosing container loop must stop,
// because the iterator did not advance and atEnd() would never become true.
// Containers always count as consumed (begin*() already moved the parent's
// iterator past them), so a truncated container is reported as consumed to
// its parent and the rest of the reply is still decoded.
static bool demarshallElement(const QDBusArgument &argument, QJsonValue &out)
{
    switch (argument.currentType()) {
    case QDBusArgument::BasicType: {
        const QVariant value = extractBasic(argument);
        if (!value.isValid()) {
            out = unsupportedElement(argument.currentSignature());
            return false;
        }
        out = DBusBridge::variantToJson(value);
        return true;
    }
    case QDBusArgument::VariantType: {
        QDBusVariant value;
        argument >> value;
        out = DBusBridge::variantToJson(value.variant());
        return true;
    }
    case QDBusArgument::ArrayType:
    case QDBusArgument::StructureType: {
        const bool isArray = argument.currentType() == QDBusArgument::ArrayType;
        QJsonArray array;
        if (isArray)
            argument.beginArray();
        else
            argument.beginStructure();
        while (!argument.atEnd()) {
            QJsonValue element;
            const bool consumed = demarshallElement(argument, element);
            array.append(element);
            if (!consumed)
                break;
        }
        if (isArray)
            argument.endArray();
        else
            argument.endStructure();
        out = array;
        return true;
    }
    case QDBusArgument::MapType: {
        QJsonObject object;
        argument.beginMap();
        while (!argument.atEnd()) {
            argument.beginMapEntry();
            const QVariant key = extractBasic(argument); // map keys are basic by spec
            QJsonValue value;
            const bool consumed = key.isValid() && demarshallElement(argument, value);
            object.insert(key.isValid() ? key.toString() : QStringLiteral("<unsupported key>"),
                          value);
            argument.endMapEntry();
            if (!consumed)
                break;
        }
        argument.endMap();
        out = object;
        return true;
    }
    default:
        out = unsupportedElement(argument.currentSignature());
        return false;
    }
}

QJsonValue DBusBridge::demarshall(const QDBusArgument &argument)
{
    QJsonValue out;
    demarshallElement(argument, out);
    return out;
}
```

- [ ] **Step 5: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R test_demarshall`
Expected: PASS (14 tests), chacun en moins de 2 s ; build sans warning.

- [ ] **Step 6: Vérifier que l'ABI exportée n'a pas bougé**

Run:
```bash
git stash && cmake --build build && nm -D --defined-only build/bin/libplasma-mcp-bridge-core.so.0.1.0 | awk '{print $3}' | sort > /tmp/abi-before.txt \
  ; git stash pop && cmake --build build && nm -D --defined-only build/bin/libplasma-mcp-bridge-core.so.0.1.0 | awk '{print $3}' | sort > /tmp/abi-after.txt \
  ; diff /tmp/abi-before.txt /tmp/abi-after.txt && echo ABI-IDENTICAL
```
Expected: `ABI-IDENTICAL`.

- [ ] **Step 7: Commit**

```bash
git add src/dbus/dbusbridge.cpp tests/
git commit -m "fix(dbus): stop infinite loop on nested string/byte arrays (B1)

QDBusDemarshaller reports 'as' and 'ay' as BasicType, so extractBasic()
read a QString without advancing the iterator and container loops never
reached atEnd(). Read them as QStringList/QByteArray, never read blindly,
and stop a container loop when an element was not consumed.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: M7 — clés de map `o`, `g` et non-chaînes

**Files:**
- Modify: `tests/test_demarshall.py` (ajout de 3 tests)
- Modify: `src/dbus/dbusbridge.cpp` (branche `MapType` de `demarshallElement` + nouvelle fonction)

**Interfaces:**
- Consumes: `demarshallElement` (Task 3).
- Produces: `static QString mapKeyToString(const QVariant &key)` dans `dbusbridge.cpp`.

- [ ] **Step 1: Ajouter les tests à la classe `Demarshall`**

```python
    # --- M7: non-string map keys -----------------------------------------
    def test_map_object_path_key(self):
        self.assertEqual(self.ret('RetMapOAS'), {'/p': ['x']})

    def test_map_signature_key(self):
        self.assertEqual(self.ret('RetMapGAS'), {'a{sv}': ['x']})

    def test_map_int_key(self):
        self.assertEqual(self.ret('RetMapIS'), {'1': 'a', '2': 'b'})
```

- [ ] **Step 2: Vérifier l'échec**

Run: `ctest --test-dir build --output-on-failure -R test_demarshall`
Expected: FAIL sur `test_map_object_path_key` (`{'': ['x']}`) et `test_map_signature_key` (`{'': ['x']}`) ; `test_map_int_key` passe déjà (il verrouille le comportement).

- [ ] **Step 3: Implémenter**

Ajouter, juste avant `static bool demarshallElement(...)` :

```cpp
// JSON object keys are strings. QVariant::toString() knows nothing about
// QDBusObjectPath / QDBusSignature and returns "" for them, so unwrap those.
static QString mapKeyToString(const QVariant &key)
{
    if (key.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return key.value<QDBusObjectPath>().path();
    if (key.metaType() == QMetaType::fromType<QDBusSignature>())
        return key.value<QDBusSignature>().signature();
    return key.toString();
}
```

Dans la branche `QDBusArgument::MapType`, remplacer :

```cpp
            object.insert(key.isValid() ? key.toString() : QStringLiteral("<unsupported key>"),
                          value);
```

par :

```cpp
            object.insert(key.isValid() ? mapKeyToString(key) : QStringLiteral("<unsupported key>"),
                          value);
```

- [ ] **Step 4: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: tous les tests PASS ; build sans warning.

- [ ] **Step 5: Commit**

```bash
git add src/dbus/dbusbridge.cpp tests/test_demarshall.py
git commit -m "fix(dbus): keep object-path and signature map keys (M7)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: CI, version 0.1.90, CHANGELOG et documentation

**Files:**
- Modify: `.github/workflows/build.yml`
- Modify: `CMakeLists.txt:4` (version)
- Create: `CHANGELOG.md`
- Modify: `README.md` (section « Build & install »), `CLAUDE.md` (sections « Build, run, test » et « Conventions »)

**Interfaces:**
- Consumes: la suite de tests (Tasks 1-4).
- Produces: `CHANGELOG.md` avec une section `## 0.2.0 (unreleased)` que chaque PR suivante complète ; version de projet `0.1.90` (convention KDE de pré-version ; CMake exige une version numérique, d'où `0.1.90` plutôt que `0.2.0-dev`).

- [ ] **Step 1: Mettre à jour la version** — dans `CMakeLists.txt`, remplacer :

```cmake
project(plasma-mcp-bridge VERSION 0.1.0 LANGUAGES CXX)
```

par :

```cmake
# 0.1.90 = pre-release of 0.2.0 (see CHANGELOG.md).
project(plasma-mcp-bridge VERSION 0.1.90 LANGUAGES CXX)
```

- [ ] **Step 2: Créer `CHANGELOG.md`**

```markdown
# Changelog

## 0.2.0 (unreleased)

Pre-releases are versioned 0.1.90+.

### Fixed
- A reply containing a string or byte array nested in an array or struct
  (`aas`, `(as)`, `aay`) no longer hangs the server in an infinite loop; such
  values in maps (`a{sas}`, `a{say}`) are no longer replaced by `""`.
- Map keys of type object path or signature (`a{oas}`, `a{gas}`) are kept
  instead of becoming `""`.

### Changed
- A reply element whose D-Bus type cannot be represented is replaced by
  `"<unsupported D-Bus type '<signature>'>"`; the rest of the reply is still
  decoded.

### Added
- Test-suite on a private D-Bus (`ctest`), see README.
```

- [ ] **Step 3: Mettre à jour la CI** — remplacer le contenu de `.github/workflows/build.yml` par :

```yaml
name: build

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build \
            extra-cmake-modules qt6-base-dev qt6-base-dev-tools \
            dbus-daemon python3-dbus python3-gi

      - name: Configure
        run: cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DPython3_EXECUTABLE=/usr/bin/python3

      - name: Build
        run: cmake --build build

      - name: Smoke test (protocol)
        run: |
          out=$(printf '%s\n' \
            '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{}}}' \
            '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
            | ./build/bin/plasma-mcp-bridge 2>/dev/null)
          echo "$out"
          echo "$out" | grep -q '"name":"plasma-mcp-bridge"'
          echo "$out" | grep -q '"name":"dbus_call"'

      - name: Tests
        run: ctest --test-dir build --output-on-failure
```

- [ ] **Step 4: README** — dans la section `## Build & install`, après le bloc de commandes existant, ajouter :

````markdown
### Tests

The test-suite runs on a throw-away private D-Bus (it never touches your
session or system bus) and needs `dbus-run-session` (package `dbus-daemon`),
`python3-dbus` and `python3-gi`:

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Without those packages the tests are skipped with a CMake warning.
````

- [ ] **Step 5: CLAUDE.md** — remplacer le paragraphe qui commence par `There is no unit-test suite yet.` et le bloc `printf` qui suit par :

````markdown
Tests: `ctest --test-dir build --output-on-failure`. Each module in `tests/`
is a Python `unittest` run by `tests/run_with_bus.sh` on a private session bus
(no activatable services; the system bus is redirected onto it). Fixtures live
in `tests/fixtures/` (`echo_service.py` is the D-Bus oracle: `Echo*` methods
return the wire signature they received, `Ret*` methods return typed values).
`tests/mcp_session.py` is the MCP client; a reply that misses its deadline is a
test failure and kills the bridge. New test module = one new line in the
`_tests` list of `tests/CMakeLists.txt`. Assert on D-Bus error *names*, never
on error messages (they differ between dbus-daemon and dbus-broker).

Quick manual smoke test (stdout is protocol, stderr is logs):

```sh
printf '%s\n' \
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{}}}' \
'{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
| ./build/bin/plasma-mcp-bridge 2>/dev/null
```
````

Et ajouter à la fin de la section `## Conventions` :

```markdown
- The code must build with `-Werror` on Qt 6.4 (CI) and on current Qt. Do not
  use `qAsConst`, `_qs` or `Q_FOREACH`; deprecation warnings are pinned to the
  6.4 level in the top-level `CMakeLists.txt`.
```

- [ ] **Step 6: Vérifier localement puis en Qt 6.4.2**

Run (local) : `cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" && cmake --build build && ctest --test-dir build --output-on-failure`
Expected: tous les tests PASS.

Run (Qt 6.4.2, comme la CI) :
```bash
docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update -qq && apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build \
    extra-cmake-modules qt6-base-dev qt6-base-dev-tools dbus-daemon python3-dbus python3-gi >/dev/null &&
  cmake -S . -B /tmp/b -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DPython3_EXECUTABLE=/usr/bin/python3 &&
  cmake --build /tmp/b && ctest --test-dir /tmp/b --output-on-failure'
```
Expected: build sans warning, tous les tests PASS. (Le build se fait dans `/tmp/b` du conteneur pour ne pas laisser de fichiers root dans l'arbre.)

- [ ] **Step 7: Commit**

```bash
git add .github/workflows/build.yml CMakeLists.txt CHANGELOG.md README.md CLAUDE.md
git commit -m "ci: run the test-suite; version 0.1.90; changelog and test docs

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Fin de la PR1

- [ ] `git log --oneline main..` montre 5 commits ; `git status` est propre.
- [ ] La description de la PR rappelle la spec (`remediation/spec` @ `0f58c04`, §3, §4.1) et liste B1 et M7 ; elle se termine par `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- [ ] Ne pas pousser ni ouvrir la PR sans l'accord explicite de l'utilisateur.
