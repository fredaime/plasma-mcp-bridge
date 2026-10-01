# PR6 — Serveur concurrent, erreurs de protocole, versions MCP, timeouts, arrêt propre — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ne plus bloquer le serveur pendant un appel lent (M4) : `tools/call` sur des workers, `ping`/`tools/list` servis tout de suite, annulation, timeouts réglables ; répondre aux frames invalides (m3) ; négocier trois versions MCP (m8, partie D) ; survivre à un stdout fermé (m11) ; refuser les noms d'outils en double (m13) ; s'arrêter en moins de 3 s même avec un appel en cours.

**Architecture:** Le thread principal garde la lecture de stdin, le dispatch et l'écriture de stdout. Une nouvelle unité `src/mcp/toolrunner.{h,cpp}` exécute chaque `tools/call` dans un `QThreadPool` de 4 workers (outils intégrés) ou dans un pool d'un worker (outils de plugins, sérialisés) et renvoie le résultat au thread principal par `QMetaObject::invokeMethod(…, Qt::QueuedConnection)`. `src/mcp/protocolversion.{h,cpp}` porte la négociation. `Server` valide les frames, suit les ids en vol, et gère l'arrêt (drain de 2 s puis `std::_Exit(0)` si un outil tourne encore). `Tool::call()` reste synchrone (ABI).

**Tech Stack:** C++17, Qt 6 Core + DBus (≥ 6.4 ; `QThreadPool`, `QRunnable::create`), CMake, tests Python du harnais (PR1) sur bus privé, un plugin de test construit dans `tests/`.

**Spec:** `docs/superpowers/specs/2026-10-01-remediation-core-design.md` — §6 (sous-projet D), §2 (M4, m3, m8, m11, m13), §8 (ABI), §9 (changelog), §10 (PR6).

## Global Constraints

- **Base :** la PR5 doit être mergée. Créer/rebaser la branche `remediation/pr6-concurrent-server` sur `main` **après** le merge de la PR5 (ce plan en est le premier commit, posé sur `1e3f0e4`). Le code ci-dessous suppose l'état post-PR5 : `DBusBackend(const CallPolicy *)`, `DBusCallTool(DBusBridge *, const CallPolicy *)`, `PluginLoader::load(path, &backends)` → `bool`, `resolveMethod(busConnection(bus), …)` dans `DBusCallTool::call`. Si un extrait ne correspond pas au code réel, la spec et le comportement décrit font foi (ledger : `Ruling:`).
- **DCO :** tous les commits avec `git commit -s` (identité `fredaime <frederic.aime@gmail.com>`), terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Seule dépendance : Qt 6 Core + DBus. `-Wall -Wextra -Werror` sur Qt 6.4.2 (CI) et Qt 6.11 (local). Pas de `QThreadPool::globalInstance()`.
- **ABI :** aucun membre de données ajouté aux en-têtes installés (`core/*.h`, `mcp/tool.h`, `mcp/toolregistry.h`, `dbus/dbusbridge.h`) ; seul ajout autorisé : la surcharge non virtuelle, sans paramètre par défaut, `DBusBridge::callMethod(…, const QJsonArray &args, int timeoutMs)`. `ToolRegistry::add()` change de comportement (doublon refusé), pas de signature. IID inchangé. `Tool::call()` inchangé.
- Seul `StdioTransport` écrit sur stdout, et seulement depuis le thread principal.
- Concurrence : 4 workers pour les outils intégrés, 1 worker (file FIFO) pour les outils de plugins. Pas de plafond de file.
- Versions supportées : `2024-11-05`, `2025-06-18`, `2025-11-25` (pas `2025-03-26`). `server/discover` reste `-32601` (PR6b).
- Messages exacts : `-32700` « `Parse error` » ; `-32600` frame non-objet « `Invalid request: a frame must be one JSON-RPC object (batches are not supported)` », requête mal formée « `Invalid request: 'method' must be a non-empty string and 'id' a string or a number` » ; `-32602` « `tools/call takes {"name": string, "arguments": object}` » et « `Unknown tool: <nom>` » ; `timeout_ms` invalide (résultat `isError`) « `'timeout_ms' must be an integer between 1 and 2147483647` » ; `--call-timeout-ms` invalide (stderr, exit 2) « `invalid --call-timeout-ms value '<v>': expected a positive integer` ».
- Lignes stderr exactes (via `qInfo`, jamais `qWarning`, pour rester compatibles avec `QT_FATAL_WARNINGS=1`) : `plasma-mcp-bridge: ignoring a JSON-RPC response from the client (id <id>)`, `plasma-mcp-bridge: cancelled request <id>`, `plasma-mcp-bridge: ignoring cancellation of request <id> (unknown or finished)`, `plasma-mcp-bridge: ignoring request <id>: a request with this id is still in flight`, `plasma-mcp-bridge: exiting with a tool call still running`, `plasma-mcp-bridge: cannot write to stdout (<errno>); shutting down`. Seul le doublon de nom d'outil est un `qWarning` : `plasma-mcp-bridge: refusing a second tool named '<nom>'`. `<id>` = l'id en JSON compact (`7`, `"a"`).
- **Décision sur une question ouverte de la spec** (consignée en Task 7) : le timeout d'un `dbus_call` borne **chaque aller-retour D-Bus**, y compris l'introspection qui résout l'interface (la PR5 en fait jusqu'à deux) ; pas de timeout d'introspection séparé.
- Tests : bus privé uniquement ; assertions sur les noms d'erreur D-Bus, jamais leurs messages ; mesures de durée avec des marges larges (CI lente).
- Conséquence hors dépôt : le schéma de `dbus_call` gagne `timeout_ms` → `skill/SKILL.md` de l'enterprise à régénérer au prochain bump du sous-module ; les outils du plugin enterprise s'exécuteront sur un worker (sérialisés).

## Review Focus

- **Id réutilisé par une autre méthode** pendant qu'un `tools/call` de même id est en vol (`ping` avec cet id) : ignoré, une seule réponse par id → `test_ping_with_an_in_flight_id_is_ignored` (Task 4).
- **Annulation d'une requête déjà terminée** : ignorée, aucune réponse, ligne stderr → `test_cancel_of_finished_request_is_ignored` (Task 4).
- **Ids `"1"` et `1`** en vol en même temps : deux requêtes distinctes, deux réponses → `test_string_and_number_ids_are_distinct` (Task 4).
- **Frame coupée en deux écritures** et fin de ligne `\r\n` : reconstituée et traitée normalement → `test_frame_split_across_writes`, `test_crlf_line_ending` (Task 1).
- **Résultat qui arrive juste après l'EOF de stdin** : encore écrit pendant le drain, sortie en code 0 → `test_result_arriving_during_drain_is_written` (Task 6).

---

## File Structure

| Fichier | Responsabilité |
|---|---|
| `src/mcp/protocolversion.h/.cpp` (créés) | Versions supportées, négociation. Pur. |
| `src/mcp/toolrunner.h/.cpp` (créés) | Pools de workers, ids en vol, annulation, retour des résultats au thread principal. |
| `src/mcp/server.h/.cpp` (modifiés) | Validation des frames et requêtes, dispatch, ids en vol, arrêt. |
| `src/mcp/stdiotransport.h/.cpp` (modifiés) | Signal `invalidFrame(int)` ; écriture vérifiée (m11). |
| `src/mcp/jsonrpc.h/.cpp` (modifiés) | `idText(id)`. |
| `src/mcp/toolregistry.cpp` (modifié) | Doublon refusé (m13). |
| `src/dbus/dbusbridge.h/.cpp`, `src/dbus/interfaceresolver.h/.cpp` (modifiés) | Timeout par aller-retour. |
| `src/tools/dbustools.h/.cpp`, `src/backends/dbusbackend.h/.cpp` (modifiés) | `timeout_ms`, timeout par défaut. |
| `src/main.cpp` (modifié) | `SIGPIPE` ignoré, `--call-timeout-ms`, outils de plugins sérialisés, arrêt. |
| `src/CMakeLists.txt` (modifié) | `mcp/protocolversion.cpp`, `mcp/toolrunner.cpp`. |
| `tests/plugin/testplugin.cpp` (créé) | Plugin de test : `test_sleep_a`, `test_sleep_b`, et un outil nommé `dbus_call` (doublon). |
| `tests/mcp_session.py` (modifié) | `env`, `start`, `collect`, `expect_silence`. |
| `tests/test_protocol.py`, `tests/test_async.py` (créés) | Protocole et versions ; concurrence, annulation, timeouts, arrêt. |
| `tests/CMakeLists.txt` (modifié) | Plugin de test, deux lignes de `_tests`, `PLASMA_MCP_TEST_PLUGIN`. |
| `README.md`, `CLAUDE.md`, `CHANGELOG.md`, spec (modifiés) | Protocole, threads, options. |

Commandes utiles (depuis la racine du worktree) :

- construire : `cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" && cmake --build build`
- une classe : `cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge PLASMA_MCP_TEST_PLUGIN=$PWD/../build/bin/plasma_mcp_test_plugin.so ./run_with_bus.sh python3 -m unittest -v test_async.Concurrency; cd ..` (emplacement du plugin : `find build -name 'plasma_mcp_test_plugin.so'`)
- tout : `ctest --test-dir build --output-on-failure --no-tests=error`

---

### Task 1: Erreurs de protocole (m3) et négociation de version (m8)

**Files:**
- Create: `src/mcp/protocolversion.h`, `src/mcp/protocolversion.cpp`, `tests/test_protocol.py`
- Modify: `src/mcp/jsonrpc.h`, `src/mcp/jsonrpc.cpp`, `src/mcp/stdiotransport.h`, `src/mcp/stdiotransport.cpp`, `src/mcp/server.h`, `src/mcp/server.cpp`, `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MCPSession(initialize=False)`, `send`, `send_raw`, `read_message`, `request` (PR1).
- Produces: `QString mcp::jsonrpc::idText(const QJsonValue &id)` ; `QStringList mcp::supportedProtocolVersions()`, `QString mcp::negotiateProtocolVersion(const QJsonValue &requested)` ; signal `StdioTransport::invalidFrame(int code)` ; `Server::handleToolsCall(const QJsonValue &id, const QJsonValue &params)` et `Server::sendToolResult(const QJsonValue &id, const QString &text, bool isError)`.

- [ ] **Step 1: Écrire les tests qui échouent**

`tests/test_protocol.py` :

```python
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
```

Dans `tests/CMakeLists.txt`, ajouter `test_protocol` en fin de `_tests` (après `test_policy`).

- [ ] **Step 2: Vérifier l'échec**

Run: `cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" >/dev/null && cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_protocol; cd ..`
Expected: ERROR `MCPTimeout` pour `test_invalid_json`, `test_batches_are_rejected`, `test_malformed_requests` (aucune réponse aujourd'hui) ; FAIL pour `test_tools_call_params` (les sous-tests où `arguments` n'est pas un objet exécutent l'outil au lieu de répondre `-32602`), `test_stray_response_gets_no_reply` (ligne stderr absente) et `test_negotiation` (`2025-06-18` → `2024-11-05`) ; PASS pour `test_unknown_tool`, `test_unknown_method`, `test_unknown_notification_is_ignored`, `test_jsonrpc_member_is_optional`, `test_frame_split_across_writes`, `test_crlf_line_ending` (comportements à préserver).

- [ ] **Step 3: `idText` et la négociation**

`src/mcp/jsonrpc.h` — après `makeError` :

```cpp
// The id as compact JSON (7, "a"): keeps 1 and "1" apart as a key, and is
// what log lines print.
QString idText(const QJsonValue &id);
```

`src/mcp/jsonrpc.cpp` — ajouter `#include <QJsonArray>` et `#include <QJsonDocument>`, puis dans le namespace :

```cpp
QString idText(const QJsonValue &id)
{
    const QByteArray json = QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
```

`src/mcp/protocolversion.h` :

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace mcp {

// MCP revisions this server speaks, oldest first. 2025-03-26 is left out on
// purpose: it requires receiving JSON-RPC batches, which later revisions
// removed and this server rejects.
QStringList supportedProtocolVersions();

// The version answered to `initialize`: the requested one when supported;
// otherwise the highest supported one older than the request (a client
// capped at 2025-03-26 gets 2024-11-05); otherwise the latest. Never fails.
QString negotiateProtocolVersion(const QJsonValue &requested);

} // namespace mcp
```

`src/mcp/protocolversion.cpp` :

```cpp
// SPDX-License-Identifier: MIT
#include "mcp/protocolversion.h"

#include <QRegularExpression>

namespace mcp {

QStringList supportedProtocolVersions()
{
    return {QStringLiteral("2024-11-05"), QStringLiteral("2025-06-18"),
            QStringLiteral("2025-11-25")};
}

QString negotiateProtocolVersion(const QJsonValue &requested)
{
    const QStringList supported = supportedProtocolVersions();
    const QString version = requested.toString();
    if (supported.contains(version))
        return version;
    // Revisions are dates: between well-formed ones, string order is date order.
    static const QRegularExpression date(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    if (date.match(version).hasMatch()) {
        for (auto it = supported.crbegin(); it != supported.crend(); ++it) {
            if (*it < version)
                return *it;
        }
    }
    return supported.last();
}

} // namespace mcp
```

Dans `src/CMakeLists.txt`, ajouter `mcp/protocolversion.cpp` après `mcp/jsonrpc.cpp`.

- [ ] **Step 4: Signaler les frames invalides**

`src/mcp/stdiotransport.h` — dans `Q_SIGNALS`, après `messageReceived` :

```cpp
    // A line that is not valid JSON (ParseError) or not a JSON object, such
    // as a batch (InvalidRequest); codes from mcp/jsonrpc.h.
    void invalidFrame(int code);
```

`src/mcp/stdiotransport.cpp` — ajouter `#include "mcp/jsonrpc.h"` et remplacer le traitement d'une ligne :

```cpp
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qInfo("plasma-mcp-bridge: invalid JSON frame: %s",
                  qUtf8Printable(parseError.errorString()));
            Q_EMIT invalidFrame(mcp::jsonrpc::ParseError);
            continue;
        }
        if (!doc.isObject()) {
            Q_EMIT invalidFrame(mcp::jsonrpc::InvalidRequest);
            continue;
        }
        Q_EMIT messageReceived(doc.object());
```

- [ ] **Step 5: Valider les requêtes dans `Server`**

`src/mcp/server.h` — remplacer la section privée :

```cpp
private:
    void onMessage(const QJsonObject &message);
    void onInvalidFrame(int code);
    void handleInitialize(const QJsonValue &id, const QJsonObject &params);
    void handleToolsList(const QJsonValue &id);
    void handleToolsCall(const QJsonValue &id, const QJsonValue &params);
    void sendToolResult(const QJsonValue &id, const QString &text, bool isError);

    StdioTransport *m_transport;
    ToolRegistry *m_registry;
};
```

`src/mcp/server.cpp` — remplacer les includes et l'espace anonyme (suppression de `kDefaultProtocolVersion` et `isSupportedProtocolVersion`) par :

```cpp
#include "mcp/server.h"

#include "mcp/jsonrpc.h"
#include "mcp/protocolversion.h"
#include "mcp/stdiotransport.h"
#include "mcp/tool.h"
#include "mcp/toolregistry.h"

#include <QJsonArray>

#ifndef PLASMA_MCP_BRIDGE_VERSION
#define PLASMA_MCP_BRIDGE_VERSION "0.0.0"
#endif
```

puis remplacer le constructeur, `onMessage`, `handleToolsCall`, et dans `handleInitialize` les deux lignes du calcul de `protocolVersion` :

```cpp
Server::Server(StdioTransport *transport, ToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_registry(registry)
{
    connect(m_transport, &StdioTransport::messageReceived, this, &Server::onMessage);
    connect(m_transport, &StdioTransport::invalidFrame, this, &Server::onInvalidFrame);
}

void Server::onInvalidFrame(int code)
{
    m_transport->send(mcp::jsonrpc::makeError(
        QJsonValue(), code,
        code == mcp::jsonrpc::ParseError
            ? QStringLiteral("Parse error")
            : QStringLiteral("Invalid request: a frame must be one JSON-RPC object (batches "
                             "are not supported)")));
}

void Server::onMessage(const QJsonObject &message)
{
    const QJsonValue method = message.value(QStringLiteral("method"));
    const QJsonValue id = message.value(QStringLiteral("id"));

    if (!message.contains(QStringLiteral("method"))
        && (message.contains(QStringLiteral("result"))
            || message.contains(QStringLiteral("error")))) {
        // This server sends no requests: a response from the client is stray.
        qInfo("plasma-mcp-bridge: ignoring a JSON-RPC response from the client (id %s)",
              qUtf8Printable(mcp::jsonrpc::idText(id)));
        return;
    }

    const bool hasId = message.contains(QStringLiteral("id"));
    const bool validId = id.isString() || id.isDouble();
    if (!method.isString() || method.toString().isEmpty() || (hasId && !validId)) {
        m_transport->send(mcp::jsonrpc::makeError(
            validId ? id : QJsonValue(), mcp::jsonrpc::InvalidRequest,
            QStringLiteral("Invalid request: 'method' must be a non-empty string and 'id' a "
                           "string or a number")));
        return;
    }

    const QString name = method.toString();
    const QJsonValue params = message.value(QStringLiteral("params"));
    if (!hasId)
        return; // notifications/initialized and every other notification: nothing to do.

    if (name == QLatin1String("initialize")) {
        handleInitialize(id, params.toObject());
    } else if (name == QLatin1String("ping")) {
        m_transport->send(mcp::jsonrpc::makeResult(id, QJsonObject{}));
    } else if (name == QLatin1String("tools/list")) {
        handleToolsList(id);
    } else if (name == QLatin1String("tools/call")) {
        handleToolsCall(id, params);
    } else {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::MethodNotFound, QStringLiteral("Method not found: %1").arg(name)));
    }
}
```

```cpp
    const QString protocolVersion =
        mcp::negotiateProtocolVersion(params.value(QStringLiteral("protocolVersion")));
```

```cpp
void Server::handleToolsCall(const QJsonValue &id, const QJsonValue &params)
{
    const QJsonValue name = params.toObject().value(QStringLiteral("name"));
    const QJsonValue arguments = params.toObject().value(QStringLiteral("arguments"));
    if (!params.isObject() || !name.isString()
        || !(arguments.isUndefined() || arguments.isNull() || arguments.isObject())) {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::InvalidParams,
            QStringLiteral("tools/call takes {\"name\": string, \"arguments\": object}")));
        return;
    }

    Tool *tool = m_registry->find(name.toString());
    if (!tool) {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::InvalidParams,
            QStringLiteral("Unknown tool: %1").arg(name.toString())));
        return;
    }

    const ToolResult result = tool->call(arguments.toObject());
    sendToolResult(id, result.text, result.isError);
}

void Server::sendToolResult(const QJsonValue &id, const QString &text, bool isError)
{
    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), text);

    QJsonObject result;
    result.insert(QStringLiteral("content"), QJsonArray{content});
    result.insert(QStringLiteral("isError"), isError);

    m_transport->send(mcp::jsonrpc::makeResult(id, result));
}
```

- [ ] **Step 6: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS, dont `test_protocol` (12 tests) et `test_smoke` (`2024-11-05` reste renvoyé tel quel).

- [ ] **Step 7: Commit**

```bash
git add src/mcp/protocolversion.h src/mcp/protocolversion.cpp src/mcp/jsonrpc.h src/mcp/jsonrpc.cpp src/mcp/stdiotransport.h src/mcp/stdiotransport.cpp src/mcp/server.h src/mcp/server.cpp src/CMakeLists.txt tests/test_protocol.py tests/CMakeLists.txt
git commit -s -m "Answer invalid frames with -32700/-32600/-32602; negotiate MCP 2024-11-05, 2025-06-18, 2025-11-25 (m3, m8)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Plugin de test et noms d'outils en double (m13)

**Files:**
- Create: `tests/plugin/testplugin.cpp`, `tests/test_async.py`
- Modify: `src/mcp/toolregistry.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FixtureTestCase`, `MCPSession.request`, `stderr_text` (PR1) ; l'ABI de plugin (`core/plugin.h`, `core/backend.h`, `mcp/tool.h`, `mcp/toolregistry.h`).
- Produces: module `plasma_mcp_test_plugin` (outils `test_sleep_a`, `test_sleep_b` : argument `ms`, réponse `"<début>-<fin>"` en ms depuis l'epoch, ligne stderr `test-plugin: <nom> ran` ; outil `dbus_call` de description `Test: clashes with a built-in tool name.`) ; variable d'environnement `PLASMA_MCP_TEST_PLUGIN` ; en Python `TEST_PLUGIN` et la classe `Plugins` dans `test_async.py`.

- [ ] **Step 1: Écrire le plugin de test**

`tests/plugin/testplugin.cpp` :

```cpp
// SPDX-License-Identifier: MIT
// Minimal plugin for the test-suite: tools that sleep, to observe how the
// bridge schedules plugin tools, and a tool whose name clashes with a
// built-in one.
#include "core/backend.h"
#include "core/plugin.h"
#include "mcp/tool.h"
#include "mcp/toolregistry.h"

#include <QDateTime>
#include <QObject>
#include <QThread>

namespace {

class SleepTool : public Tool
{
public:
    explicit SleepTool(const QString &name) : m_name(name) {}
    QString name() const override { return m_name; }
    QString description() const override
    {
        return QStringLiteral("Test: sleep 'ms' milliseconds; reply \"<start>-<end>\" in ms "
                              "since the epoch.");
    }
    QJsonObject inputSchema() const override
    {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
    }
    ToolResult call(const QJsonObject &arguments) override
    {
        qInfo("test-plugin: %s ran", qUtf8Printable(m_name));
        const qint64 start = QDateTime::currentMSecsSinceEpoch();
        QThread::msleep(static_cast<unsigned long>(arguments.value(QStringLiteral("ms")).toInt()));
        return ToolResult::ok(
            QStringLiteral("%1-%2").arg(start).arg(QDateTime::currentMSecsSinceEpoch()));
    }

private:
    QString m_name;
};

class ClashTool : public SleepTool
{
public:
    ClashTool() : SleepTool(QStringLiteral("dbus_call")) {}
    QString description() const override
    {
        return QStringLiteral("Test: clashes with a built-in tool name.");
    }
};

class TestBackend : public Backend
{
public:
    QString name() const override { return QStringLiteral("test"); }
    QString description() const override { return QStringLiteral("Test-suite plugin."); }
    void registerTools(ToolRegistry *registry, const BridgeContext &) override
    {
        registry->add(std::make_unique<SleepTool>(QStringLiteral("test_sleep_a")));
        registry->add(std::make_unique<SleepTool>(QStringLiteral("test_sleep_b")));
        registry->add(std::make_unique<ClashTool>());
    }
};

} // namespace

class TestPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PLASMA_MCP_BRIDGE_PLUGIN_IID)
    Q_INTERFACES(PluginInterface)

public:
    std::vector<std::unique_ptr<Backend>> createBackends() override
    {
        std::vector<std::unique_ptr<Backend>> backends;
        backends.push_back(std::make_unique<TestBackend>());
        return backends;
    }
};

#include "testplugin.moc"
```

Dans `tests/CMakeLists.txt`, après le bloc `find_program(DBUS_RUN_SESSION_EXECUTABLE …)` / `endif()` :

```cmake
# Minimal plugin for test_async (tool scheduling, duplicate tool names).
add_library(plasma_mcp_test_plugin MODULE plugin/testplugin.cpp)
target_link_libraries(plasma_mcp_test_plugin PRIVATE PlasmaMcpBridge::PluginInterface)
set_target_properties(plasma_mcp_test_plugin PROPERTIES PREFIX "")
```

ajouter `test_async` en fin de `_tests`, et compléter `ENVIRONMENT` (garder les variables existantes, ajouter à la fin) :

```cmake
;PLASMA_MCP_TEST_PLUGIN=$<TARGET_FILE:plasma_mcp_test_plugin>
```

- [ ] **Step 2: Écrire le test qui échoue**

`tests/test_async.py` :

```python
# SPDX-License-Identifier: MIT
"""Tool calls off the main thread: concurrency, plugins, cancellation, timeouts, shutdown."""
import os
import unittest

from mcp_session import FixtureTestCase

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


if __name__ == '__main__':
    unittest.main()
```

Run: `cmake -S . -B build >/dev/null && cmake --build build && ctest --test-dir build -R test_async --output-on-failure`
Expected: le plugin se construit sans warning ; FAIL `2 != 1` (les deux `dbus_call` sont listés).

- [ ] **Step 3: Refuser le doublon**

`src/mcp/toolregistry.cpp` — remplacer `add` :

```cpp
void ToolRegistry::add(std::unique_ptr<Tool> tool)
{
    if (!tool)
        return;
    const QString name = tool->name();
    if (m_index.contains(name)) {
        qWarning("plasma-mcp-bridge: refusing a second tool named '%s'", qUtf8Printable(name));
        return;
    }
    m_index.insert(name, tool.get());
    m_tools.push_back(std::move(tool));
}
```

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: tous les modules PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/plugin/testplugin.cpp tests/test_async.py tests/CMakeLists.txt src/mcp/toolregistry.cpp
git commit -s -m "Refuse a second tool with the same name (m13); add a test plugin

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `tools/call` sur des workers (M4)

**Files:**
- Create: `src/mcp/toolrunner.h`, `src/mcp/toolrunner.cpp`
- Modify: `src/mcp/server.h`, `src/mcp/server.cpp`, `src/main.cpp`, `src/CMakeLists.txt`, `tests/mcp_session.py`, `tests/test_async.py`

**Interfaces:**
- Consumes: `Server::handleToolsCall`, `Server::sendToolResult` (Task 1) ; `TEST_PLUGIN`, outils `test_sleep_a/b` (Task 2) ; `idText` (Task 1).
- Produces: `class ToolRunner : QObject` avec `void submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments, bool serialized)`, signal `resultReady(const QJsonValue &id, const QString &text, bool isError)`, privé `finish(id, text, isError)` et `QSet<QString> m_inFlight` ; `Server::setSerializedTools(const QSet<QString> &)` ; en Python `MCPSession(..., env=None)`, `MCPSession.start(method, params=None, rid=None) -> rid`, `MCPSession.collect(count, timeout=2.0) -> list`, `MCPSession.expect_silence(seconds)` ; helper `tool_call(arguments, name='dbus_call')` dans `test_async.py`.

- [ ] **Step 1: Outils de test côté harnais**

Dans `tests/mcp_session.py`, `MCPSession.__init__` prend `env` et le passe à `Popen` :

```python
    def __init__(self, *extra_args, initialize=True, protocol_version='2024-11-05', env=None):
        require_private_bus()
        self._stderr = tempfile.TemporaryFile()
        self.proc = subprocess.Popen(
            [os.environ['PLASMA_MCP_BRIDGE'], *extra_args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self._stderr,
            env=dict(os.environ, **(env or {})), preexec_fn=_die_with_parent)
```

et, après `call_json` :

```python
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
```

- [ ] **Step 2: Écrire les tests qui échouent**

Dans `tests/test_async.py`, remplacer les imports par :

```python
import collections
import json
import os
import time
import unittest

from mcp_session import ECHO, FixtureTestCase
```

et ajouter, avant `if __name__ == '__main__':` :

```python
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
```

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge PLASMA_MCP_TEST_PLUGIN=$(find $PWD/../build -name plasma_mcp_test_plugin.so) ./run_with_bus.sh python3 -m unittest -v test_async.Concurrency; cd ..`
Expected: FAIL pour `test_ping_during_slow_call`, `test_builtin_calls_run_in_parallel` (≈ 4 s), `test_replies_may_come_out_of_order` (ordre d'envoi) et `test_builtin_tools_run_beside_a_plugin_tool` ; PASS pour `test_plugin_tools_do_not_overlap` et `test_stress` (tout est encore sérialisé : ces deux tests gardent ces garanties une fois les workers en place).

- [ ] **Step 3: Le runner**

`src/mcp/toolrunner.h` :

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>

class QThreadPool;
class Tool;

// Runs tools/call off the main thread: built-in tools on a pool of four
// workers, serialized tools (plugins) on a pool of one, in arrival order.
// Results come back on the runner's thread (the main thread), the only one
// that writes stdout. The pools are children of the runner: destroying it
// waits for running calls, so it must only be destroyed with none in flight.
class ToolRunner : public QObject
{
    Q_OBJECT
public:
    explicit ToolRunner(QObject *parent = nullptr);

    void submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments, bool serialized);

Q_SIGNALS:
    void resultReady(const QJsonValue &id, const QString &text, bool isError);

private:
    void finish(const QJsonValue &id, const QString &text, bool isError);

    QThreadPool *m_pool;
    QThreadPool *m_serialPool;
    QSet<QString> m_inFlight; // mcp::jsonrpc::idText of each call in flight
};
```

`src/mcp/toolrunner.cpp` :

```cpp
// SPDX-License-Identifier: MIT
#include "mcp/toolrunner.h"

#include "mcp/jsonrpc.h"
#include "mcp/tool.h"

#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

ToolRunner::ToolRunner(QObject *parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
    , m_serialPool(new QThreadPool(this))
{
    m_pool->setMaxThreadCount(4);
    m_serialPool->setMaxThreadCount(1);
}

void ToolRunner::submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments,
                        bool serialized)
{
    m_inFlight.insert(mcp::jsonrpc::idText(id));
    QThreadPool *pool = serialized ? m_serialPool : m_pool;
    pool->start(QRunnable::create([this, id, tool, arguments] {
        const ToolResult result = tool->call(arguments);
        QMetaObject::invokeMethod(
            this, [this, id, result] { finish(id, result.text, result.isError); },
            Qt::QueuedConnection);
    }));
}

void ToolRunner::finish(const QJsonValue &id, const QString &text, bool isError)
{
    m_inFlight.remove(mcp::jsonrpc::idText(id));
    Q_EMIT resultReady(id, text, isError);
}
```

Dans `src/CMakeLists.txt`, ajouter `mcp/toolrunner.cpp` après `mcp/server.cpp`.

- [ ] **Step 4: Brancher le runner dans `Server`**

`src/mcp/server.h` — ajouter `#include <QSet>` et `#include <QString>`, `class ToolRunner;` après `class ToolRegistry;`, puis :

```cpp
// Implements the MCP request handlers on top of a JSON-RPC stream. Runs on
// the main thread: it answers initialize, ping, tools/list, notifications
// and protocol errors at once, and hands every tools/call to a ToolRunner,
// so replies may come out of order (they carry the request id).
class Server : public QObject
{
    Q_OBJECT
public:
    Server(StdioTransport *transport, ToolRegistry *registry, QObject *parent = nullptr);

    // Tools contributed by plugins: they run one at a time (a plugin need not
    // be reentrant), beside the built-in tools.
    void setSerializedTools(const QSet<QString> &names);

private:
    void onMessage(const QJsonObject &message);
    void onInvalidFrame(int code);
    void handleInitialize(const QJsonValue &id, const QJsonObject &params);
    void handleToolsList(const QJsonValue &id);
    void handleToolsCall(const QJsonValue &id, const QJsonValue &params);
    void sendToolResult(const QJsonValue &id, const QString &text, bool isError);

    StdioTransport *m_transport;
    ToolRegistry *m_registry;
    ToolRunner *m_runner;
    QSet<QString> m_serializedTools;
};
```

`src/mcp/server.cpp` — ajouter `#include "mcp/toolrunner.h"` ; dans le constructeur, initialiser `m_runner(new ToolRunner(this))` après `m_registry(registry)` et ajouter la connexion :

```cpp
    connect(m_runner, &ToolRunner::resultReady, this, &Server::sendToolResult);
```

ajouter :

```cpp
void Server::setSerializedTools(const QSet<QString> &names)
{
    m_serializedTools = names;
}
```

et, dans `handleToolsCall`, remplacer les deux dernières lignes (`tool->call` et `sendToolResult`) par :

```cpp
    m_runner->submit(id, tool, arguments.toObject(),
                     m_serializedTools.contains(name.toString()));
```

- [ ] **Step 5: Marquer les outils de plugins dans `main.cpp`**

Ajouter `#include <QJsonArray>`, `#include <QJsonObject>`, `#include <QSet>` ; dans l'espace anonyme :

```cpp
QSet<QString> toolNames(const ToolRegistry &registry)
{
    QSet<QString> names;
    const QJsonArray tools = registry.toJson();
    for (const QJsonValue &tool : tools)
        names.insert(tool.toObject().value(QStringLiteral("name")).toString());
    return names;
}
```

remplacer la construction des backends et l'enregistrement (de `auto allBackends = …` à `registerAll(allBackends, &registry, context);`) par :

```cpp
    const auto builtins = builtinBackends(&policy);
    std::vector<std::unique_ptr<Backend>> pluginBackends;
    PluginLoader loader;
    for (const QString &pluginPath : parser.values(pluginOption)) {
        if (!loader.load(pluginPath, &pluginBackends)) {
            qCritical("plasma-mcp-bridge: aborting: plugin %s could not be loaded",
                      qUtf8Printable(pluginPath));
            return 2;
        }
    }

    registerAll(builtins, &registry, context);
    const QSet<QString> builtinTools = toolNames(registry);
    registerAll(pluginBackends, &registry, context);
    const QSet<QString> pluginTools = toolNames(registry) - builtinTools;
```

et, après `Server server(&transport, &registry);` :

```cpp
    server.setSerializedTools(pluginTools);
```

- [ ] **Step 6: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_async` : 7 tests).

- [ ] **Step 7: Commit**

```bash
git add src/mcp/toolrunner.h src/mcp/toolrunner.cpp src/mcp/server.h src/mcp/server.cpp src/main.cpp src/CMakeLists.txt tests/mcp_session.py tests/test_async.py
git commit -s -m "Run tools/call on worker threads; plugin tools one at a time (M4)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Annulation et ids en vol

**Files:**
- Modify: `src/mcp/toolrunner.h`, `src/mcp/toolrunner.cpp`, `src/mcp/server.h`, `src/mcp/server.cpp`, `tests/test_async.py`

**Interfaces:**
- Consumes: `ToolRunner` (Task 3) ; `start`, `collect`, `expect_silence`, `tool_call`, `TEST_PLUGIN` (Tasks 2-3).
- Produces: `bool ToolRunner::submit(...)` (false si l'id est en vol), `bool ToolRunner::cancel(const QJsonValue &id)`, `bool ToolRunner::isInFlight(const QJsonValue &id) const`, membre `QHash<QString, std::shared_ptr<std::atomic_bool>> m_inFlight` ; `Server::handleNotification(const QString &method, const QJsonObject &params)`.

- [ ] **Step 1: Écrire les tests qui échouent**

Dans `tests/test_async.py`, ajouter :

```python
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
```

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge PLASMA_MCP_TEST_PLUGIN=$(find $PWD/../build -name plasma_mcp_test_plugin.so) ./run_with_bus.sh python3 -m unittest -v test_async.Cancellation; cd ..`
Expected: FAIL pour `test_cancelled_call_gets_no_reply` (la réponse arrive), `test_queued_call_is_skipped` (`test_sleep_b ran`), `test_cancel_of_finished_request_is_ignored` (ligne stderr absente), `test_duplicate_id_in_flight` et `test_ping_with_an_in_flight_id_is_ignored` (deux réponses pour un id) ; PASS pour `test_cancel_of_initialize_is_ignored` et `test_string_and_number_ids_are_distinct`.

- [ ] **Step 2: Annulation dans le runner**

`src/mcp/toolrunner.h` — ajouter `#include <QHash>`, `#include <atomic>`, `#include <memory>` (et retirer `#include <QSet>`), puis remplacer la partie publique et le membre :

```cpp
public:
    explicit ToolRunner(QObject *parent = nullptr);

    // False, and nothing runs, when a call with this id is still in flight.
    bool submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments, bool serialized);
    // Tool::call() cannot be interrupted: a cancelled call is skipped if it
    // has not started, and its result is dropped otherwise. False when the
    // id is not in flight (unknown or finished).
    bool cancel(const QJsonValue &id);
    bool isInFlight(const QJsonValue &id) const;

Q_SIGNALS:
    // Not emitted for a cancelled call.
    void resultReady(const QJsonValue &id, const QString &text, bool isError);
```

```cpp
    // Calls in flight by mcp::jsonrpc::idText, with their cancel flag.
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_inFlight;
```

`src/mcp/toolrunner.cpp` — remplacer `submit` et `finish`, ajouter `cancel` et `isInFlight` :

```cpp
bool ToolRunner::submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments,
                        bool serialized)
{
    const QString key = mcp::jsonrpc::idText(id);
    if (m_inFlight.contains(key))
        return false;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_inFlight.insert(key, cancelled);
    QThreadPool *pool = serialized ? m_serialPool : m_pool;
    pool->start(QRunnable::create([this, id, tool, arguments, cancelled] {
        ToolResult result;
        if (!cancelled->load()) // cancelled while waiting in the queue: never starts
            result = tool->call(arguments);
        QMetaObject::invokeMethod(
            this, [this, id, result] { finish(id, result.text, result.isError); },
            Qt::QueuedConnection);
    }));
    return true;
}

bool ToolRunner::cancel(const QJsonValue &id)
{
    const auto it = m_inFlight.constFind(mcp::jsonrpc::idText(id));
    if (it == m_inFlight.constEnd())
        return false;
    it.value()->store(true);
    return true;
}

bool ToolRunner::isInFlight(const QJsonValue &id) const
{
    return m_inFlight.contains(mcp::jsonrpc::idText(id));
}

void ToolRunner::finish(const QJsonValue &id, const QString &text, bool isError)
{
    const std::shared_ptr<std::atomic_bool> cancelled = m_inFlight.take(mcp::jsonrpc::idText(id));
    if (cancelled && !cancelled->load())
        Q_EMIT resultReady(id, text, isError);
}
```

- [ ] **Step 3: Notifications et ids en vol dans `Server`**

`src/mcp/server.h` — ajouter dans la section privée :

```cpp
    void handleNotification(const QString &method, const QJsonObject &params);
```

`src/mcp/server.cpp` — dans `onMessage`, remplacer

```cpp
    if (!hasId)
        return; // notifications/initialized and every other notification: nothing to do.
```

par

```cpp
    if (!hasId) {
        handleNotification(name, params.toObject());
        return;
    }
    if (m_runner->isInFlight(id)) {
        // Exactly one response per id: the call already running keeps it.
        qInfo("plasma-mcp-bridge: ignoring request %s: a request with this id is still in flight",
              qUtf8Printable(mcp::jsonrpc::idText(id)));
        return;
    }
```

et ajouter :

```cpp
void Server::handleNotification(const QString &method, const QJsonObject &params)
{
    // notifications/initialized and every other notification: nothing to do.
    if (method != QLatin1String("notifications/cancelled"))
        return;
    const QJsonValue requestId = params.value(QStringLiteral("requestId"));
    if (m_runner->cancel(requestId))
        qInfo("plasma-mcp-bridge: cancelled request %s",
              qUtf8Printable(mcp::jsonrpc::idText(requestId)));
    else
        qInfo("plasma-mcp-bridge: ignoring cancellation of request %s (unknown or finished)",
              qUtf8Printable(mcp::jsonrpc::idText(requestId)));
}
```

(`initialize` est traité tout de suite et n'est jamais en vol : son annulation tombe dans le second cas.)

- [ ] **Step 4: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_async` : 14 tests).

- [ ] **Step 5: Commit**

```bash
git add src/mcp/toolrunner.h src/mcp/toolrunner.cpp src/mcp/server.h src/mcp/server.cpp tests/test_async.py
git commit -s -m "Honour notifications/cancelled; one response per in-flight id

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Timeouts (`--call-timeout-ms`, `timeout_ms`)

**Files:**
- Modify: `src/dbus/dbusbridge.h`, `src/dbus/dbusbridge.cpp`, `src/dbus/interfaceresolver.h`, `src/dbus/interfaceresolver.cpp`, `src/tools/dbustools.h`, `src/tools/dbustools.cpp`, `src/backends/dbusbackend.h`, `src/backends/dbusbackend.cpp`, `src/main.cpp`, `tests/test_async.py`

**Interfaces:**
- Consumes: `DBusCallTool::call` post-PR5 (policy, `busArgument`, `resolveMethod(busConnection(bus), …)`), `DBusBackend(const CallPolicy *)` (PR5) ; méthode `SlowBlocking(d)` de la fixture echo.
- Produces: `DBusResult DBusBridge::callMethod(const QString &busName, const QString &service, const QString &path, const QString &interface, const QString &method, const QJsonArray &args, int timeoutMs)` ; `MethodResolution resolveMethod(const QDBusConnection &, const QString &service, const QString &path, const QString &interface, const QString &method, int timeoutMs)` ; `DBusBackend(const CallPolicy *policy, int callTimeoutMs)` ; `DBusCallTool(DBusBridge *, const CallPolicy *, int defaultTimeoutMs)` ; propriété de schéma `timeout_ms`.

- [ ] **Step 1: Écrire les tests qui échouent**

Dans `tests/test_async.py`, ajouter `import subprocess` aux imports, puis :

```python
class CallTimeout(FixtureTestCase):
    """SlowBlocking blocks the fixture: one test per class."""

    def test_timeout_ms(self):
        session = self.bridge()
        started = time.monotonic()
        reply = session.call('dbus_call', dict(ECHO, method='SlowBlocking', args=[3],
                                               timeout_ms=1000), timeout=5)
        elapsed = time.monotonic() - started
        self.assertTrue(reply.is_error, reply.text)
        self.assertIn('org.freedesktop.DBus.Error.NoReply', reply.text)
        self.assertGreater(elapsed, 0.8)
        self.assertLess(elapsed, 2.0)


class DefaultCallTimeout(FixtureTestCase):
    """SlowBlocking blocks the fixture: one test per class."""

    def test_call_timeout_option(self):
        session = self.bridge('--call-timeout-ms', '1000')
        started = time.monotonic()
        reply = session.call('dbus_call', dict(ECHO, method='SlowBlocking', args=[3]), timeout=5)
        elapsed = time.monotonic() - started
        self.assertIn('org.freedesktop.DBus.Error.NoReply', reply.text)
        self.assertGreater(elapsed, 0.8)
        self.assertLess(elapsed, 2.0)


class TimeoutValues(FixtureTestCase):

    def test_invalid_timeout_ms(self):
        session = self.bridge()
        for value in (0, -5, 1.5, '1000', True, 2 ** 31):
            with self.subTest(value=value):
                reply = session.call('dbus_call', dict(ECHO, method='RetU', timeout_ms=value))
                self.assertTrue(reply.is_error, reply.text)
                self.assertEqual(reply.text,
                                 "'timeout_ms' must be an integer between 1 and 2147483647")

    def test_timeout_ms_in_schema(self):
        tools = self.bridge().request('tools/list')['result']['tools']
        schema = next(t for t in tools if t['name'] == 'dbus_call')['inputSchema']
        self.assertEqual(schema['properties']['timeout_ms']['type'], 'integer')

    def test_invalid_call_timeout_option_exits_2(self):
        for value in ('0', '-1', 'abc', '1.5'):
            with self.subTest(value=value):
                result = subprocess.run(
                    [os.environ['PLASMA_MCP_BRIDGE'], '--call-timeout-ms', value],
                    stdin=subprocess.DEVNULL, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(b'invalid --call-timeout-ms value', result.stderr)
```

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_async.CallTimeout test_async.DefaultCallTimeout test_async.TimeoutValues; cd ..`
Expected: FAIL pour `test_timeout_ms` (réponse après 3 s, pas d'erreur), `test_invalid_timeout_ms`, `test_timeout_ms_in_schema` (`KeyError`) et `test_invalid_call_timeout_option_exits_2` (`1 != 2`) ; ERROR `MCPTimeout`/`EOFError` pour `test_call_timeout_option` (option inconnue).

- [ ] **Step 2: Timeout par aller-retour dans le bridge**

`src/dbus/interfaceresolver.h` — la déclaration de `resolveMethod` devient :

```cpp
// Introspects service/path (one round-trip of at most timeoutMs, no cache)
// and resolves `method`.
MethodResolution resolveMethod(const QDBusConnection &bus, const QString &service,
                               const QString &path, const QString &interface,
                               const QString &method, int timeoutMs);
```

`src/dbus/interfaceresolver.cpp` — même signature, et l'appel devient `const QDBusMessage reply = bus.call(call, QDBus::Block, timeoutMs);`.

`src/dbus/dbusbridge.h` — après la déclaration de `callMethod` :

```cpp
    // Same, each D-Bus round-trip (introspection, then the call) waiting at
    // most timeoutMs; the overload above uses 25000.
    DBusResult callMethod(const QString &busName, const QString &service, const QString &path,
                          const QString &interface, const QString &method, const QJsonArray &args,
                          int timeoutMs);
```

`src/dbus/dbusbridge.cpp` — avant `DBusBridge::DBusBridge()`, ajouter :

```cpp
namespace {
// What QtDBus waits by default (its -1).
constexpr int kDefaultCallTimeoutMs = 25000;
} // namespace
```

transformer l'implémentation existante de `callMethod` en celle de la surcharge à 7 paramètres (ajouter `int timeoutMs` à la signature), avec `resolveMethod(bus, service, path, interface, method, timeoutMs)` et `bus.call(call, QDBus::Block, timeoutMs)`, puis ajouter la délégation :

```cpp
DBusResult DBusBridge::callMethod(const QString &busName, const QString &service,
                                  const QString &path, const QString &interface,
                                  const QString &method, const QJsonArray &args)
{
    return callMethod(busName, service, path, interface, method, args, kDefaultCallTimeoutMs);
}
```

- [ ] **Step 3: `timeout_ms` et `--call-timeout-ms`**

`src/backends/dbusbackend.h` — constructeur et membres :

```cpp
    DBusBackend(const CallPolicy *policy, int callTimeoutMs)
        : m_policy(policy), m_callTimeoutMs(callTimeoutMs) {}
```
```cpp
private:
    const CallPolicy *m_policy;
    int m_callTimeoutMs;
```

`src/backends/dbusbackend.cpp` — `registry->add(std::make_unique<DBusCallTool>(context.dbus, m_policy, m_callTimeoutMs));`

`src/tools/dbustools.h` — `DBusCallTool` :

```cpp
    DBusCallTool(DBusBridge *bridge, const CallPolicy *policy, int defaultTimeoutMs)
        : m_bridge(bridge), m_policy(policy), m_defaultTimeoutMs(defaultTimeoutMs) {}
```
```cpp
private:
    DBusBridge *m_bridge;
    const CallPolicy *m_policy;
    int m_defaultTimeoutMs;
```

`src/tools/dbustools.cpp` — ajouter `#include <cmath>` et `#include <limits>` ; dans `DBusCallTool::inputSchema()`, avant `properties.insert(QStringLiteral("args"), args);` :

```cpp
    QJsonObject timeout;
    timeout.insert(QStringLiteral("type"), QStringLiteral("integer"));
    timeout.insert(QStringLiteral("minimum"), 1);
    timeout.insert(QStringLiteral("description"),
                   QStringLiteral("Milliseconds to wait for each D-Bus round-trip of this call "
                                  "(introspection, then the call). Defaults to the bridge's "
                                  "--call-timeout-ms (25000)."));
    properties.insert(QStringLiteral("timeout_ms"), timeout);
```

dans `DBusCallTool::call`, juste après la validation de `bus` :

```cpp
    int timeoutMs = m_defaultTimeoutMs;
    const QJsonValue timeoutValue = arguments.value(QStringLiteral("timeout_ms"));
    if (!timeoutValue.isUndefined() && !timeoutValue.isNull()) {
        const double ms = timeoutValue.toDouble();
        if (!timeoutValue.isDouble() || ms < 1 || ms > std::numeric_limits<int>::max()
            || std::floor(ms) != ms)
            return ToolResult::failure(
                QStringLiteral("'timeout_ms' must be an integer between 1 and 2147483647"));
        timeoutMs = static_cast<int>(ms);
    }
```

et passer `timeoutMs` aux deux appels : `resolveMethod(busConnection(bus), service, path, QString(), method, timeoutMs)` et `m_bridge->callMethod(bus, service, path, target.interface, method, args, timeoutMs)`.

`src/main.cpp` — `builtinBackends(const CallPolicy *policy, int callTimeoutMs)` construit `std::make_unique<DBusBackend>(policy, callTimeoutMs)` ; ajouter l'option après `allowUniqueNamesOption` :

```cpp
    QCommandLineOption callTimeoutOption(QStringLiteral("call-timeout-ms"),
        QStringLiteral("Milliseconds dbus_call waits for each D-Bus round-trip (default 25000). "
                       "A call's timeout_ms argument overrides it."),
        QStringLiteral("ms"), QStringLiteral("25000"));
    parser.addOption(callTimeoutOption);
```

après la configuration de la policy :

```cpp
    bool callTimeoutValid = false;
    const int callTimeoutMs = parser.value(callTimeoutOption).toInt(&callTimeoutValid);
    if (!callTimeoutValid || callTimeoutMs < 1) {
        qCritical("plasma-mcp-bridge: invalid --call-timeout-ms value '%s': expected a positive "
                  "integer",
                  qUtf8Printable(parser.value(callTimeoutOption)));
        return 2;
    }
```

et `const auto builtins = builtinBackends(&policy, callTimeoutMs);`.

- [ ] **Step 4: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_async` : 19 tests).

- [ ] **Step 5: Commit**

```bash
git add src/dbus/dbusbridge.h src/dbus/dbusbridge.cpp src/dbus/interfaceresolver.h src/dbus/interfaceresolver.cpp src/tools/dbustools.h src/tools/dbustools.cpp src/backends/dbusbackend.h src/backends/dbusbackend.cpp src/main.cpp tests/test_async.py
git commit -s -m "Add --call-timeout-ms and a per-call timeout_ms to dbus_call

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Arrêt propre et `SIGPIPE` (m11)

**Files:**
- Modify: `src/mcp/toolrunner.h`, `src/mcp/toolrunner.cpp`, `src/mcp/server.h`, `src/mcp/server.cpp`, `src/mcp/stdiotransport.h`, `src/mcp/stdiotransport.cpp`, `src/main.cpp`, `tests/test_async.py`

**Interfaces:**
- Consumes: `ToolRunner`, `Server` (Task 4) ; `start`, `tool_call` (Task 3).
- Produces: `int ToolRunner::inFlight() const`, signal `ToolRunner::idle()` ; `void Server::shutdown()`, signal `Server::finished()`, membre `bool m_closing` ; `StdioTransport::send()` qui émet `closed()` sur erreur d'écriture, membre `bool m_broken`.

- [ ] **Step 1: Écrire les tests qui échouent**

Dans `tests/test_async.py`, ajouter :

```python
class ShutdownWhileBlocked(FixtureTestCase):
    """SlowBlocking blocks the fixture: one test per class."""

    def test_eof_during_a_blocking_call_exits_promptly(self):
        session = self.bridge()
        session.start('tools/call', tool_call(dict(ECHO, method='SlowBlocking', args=[10])))
        time.sleep(0.3)
        started = time.monotonic()
        session.proc.stdin.close()
        self.assertEqual(session.proc.wait(timeout=5), 0)
        self.assertLess(time.monotonic() - started, 3.0)
        self.assertIn('exiting with a tool call still running', session.stderr_text())


class Shutdown(FixtureTestCase):

    def test_result_arriving_during_drain_is_written(self):
        session = self.bridge()
        rid = session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[1])))
        session.proc.stdin.close()
        self.assertEqual(session.read_message(timeout=2.5)['id'], rid)
        self.assertEqual(session.proc.wait(timeout=2), 0)

    def test_closed_stdout_does_not_kill_with_sigpipe(self):
        session = self.bridge()
        session.start('tools/call', tool_call(dict(ECHO, method='Slow', args=[1])))
        session.proc.stdout.close()
        self.assertEqual(session.proc.wait(timeout=5), 0)
        self.assertIn('cannot write to stdout', session.stderr_text())
```

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_async.ShutdownWhileBlocked test_async.Shutdown; cd ..`
Expected: FAIL pour `test_eof_during_a_blocking_call_exits_promptly` (sortie après ≈ 10 s : `~QThreadPool` attend l'appel), `test_result_arriving_during_drain_is_written` (le bridge sort sans écrire la réponse : `MCPTimeout`/`EOFError`) et `test_closed_stdout_does_not_kill_with_sigpipe` (code `-13`).

- [ ] **Step 2: Compter les appels en vol**

`src/mcp/toolrunner.h` — dans la partie publique :

```cpp
    int inFlight() const { return static_cast<int>(m_inFlight.size()); }
```

et dans `Q_SIGNALS` :

```cpp
    // The last call in flight has finished.
    void idle();
```

`src/mcp/toolrunner.cpp` — à la fin de `finish` :

```cpp
    if (m_inFlight.isEmpty())
        Q_EMIT idle();
```

- [ ] **Step 3: Écriture vérifiée**

`src/mcp/stdiotransport.h` — ajouter le membre `bool m_broken = false;` et compléter le commentaire de `send` :

```cpp
    // On a write error (client gone), stops writing and emits closed().
    void send(const QJsonObject &message);
```

`src/mcp/stdiotransport.cpp` — ajouter `#include <cerrno>` et `#include <cstring>`, et remplacer `send` :

```cpp
void StdioTransport::send(const QJsonObject &message)
{
    if (m_broken)
        return;
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stdout)
            != static_cast<size_t>(data.size())
        || std::fflush(stdout) != 0) {
        m_broken = true;
        qInfo("plasma-mcp-bridge: cannot write to stdout (%s); shutting down",
              std::strerror(errno));
        if (m_notifier)
            m_notifier->setEnabled(false);
        Q_EMIT closed();
    }
}
```

- [ ] **Step 4: L'arrêt dans `Server`**

`src/mcp/server.h` — partie publique, après `setSerializedTools` :

```cpp
    // The client is gone (stdin closed or stdout broken): no more tools/call
    // is accepted; results that arrive within two seconds are still written.
    // Emits finished() once nothing is in flight. If a call is still running
    // after two seconds, flushes and exits the process (see server.cpp).
    void shutdown();

Q_SIGNALS:
    void finished();
```

et le membre `bool m_closing = false;`.

`src/mcp/server.cpp` — ajouter `#include <QTimer>`, `#include <cstdio>`, `#include <cstdlib>` ; dans l'espace anonyme (le créer après les `#define`) :

```cpp
namespace {
// How long results are still written after the client went away.
constexpr int kDrainMs = 2000;
} // namespace
```

dans le constructeur :

```cpp
    connect(m_runner, &ToolRunner::idle, this, [this] {
        if (m_closing)
            Q_EMIT finished();
    });
```

au début de `handleToolsCall` :

```cpp
    if (m_closing) {
        qInfo("plasma-mcp-bridge: ignoring request %s: shutting down",
              qUtf8Printable(mcp::jsonrpc::idText(id)));
        return;
    }
```

et :

```cpp
void Server::shutdown()
{
    if (m_closing)
        return;
    m_closing = true;
    if (m_runner->inFlight() == 0) {
        Q_EMIT finished();
        return;
    }
    QTimer::singleShot(kDrainMs, this, [] {
        // A call is still running. Do not destroy the thread pools (their
        // destructor waits for the D-Bus call, up to its timeout) nor return
        // from main() (the registry would be destroyed under the worker).
        qInfo("plasma-mcp-bridge: exiting with a tool call still running");
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    });
}
```

- [ ] **Step 5: `main.cpp`**

Ajouter `#include <csignal>` ; en tête de `main`, avant `QCoreApplication app(argc, argv);` :

```cpp
    // A client that closes its end must not kill the bridge mid-write (m11):
    // the write fails and the transport shuts down instead.
    std::signal(SIGPIPE, SIG_IGN);
```

et remplacer `QObject::connect(&transport, &StdioTransport::closed, &app, &QCoreApplication::quit);` par :

```cpp
    QObject::connect(&transport, &StdioTransport::closed, &server, &Server::shutdown);
    QObject::connect(&server, &Server::finished, &app, &QCoreApplication::quit);
```

- [ ] **Step 6: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_async` : 22 tests ; `test_smoke.test_eof_exits_cleanly` sort toujours en code 0).

- [ ] **Step 7: Commit**

```bash
git add src/mcp/toolrunner.h src/mcp/toolrunner.cpp src/mcp/server.h src/mcp/server.cpp src/mcp/stdiotransport.h src/mcp/stdiotransport.cpp src/main.cpp tests/test_async.py
git commit -s -m "Drain results for 2 s on shutdown, then exit even with a call running; survive a closed stdout (m11)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Documentation, CHANGELOG, spec, ABI, contrôle Qt 6.4.2

**Files:**
- Modify: `README.md`, `CLAUDE.md`, `CHANGELOG.md`, `docs/superpowers/specs/2026-10-01-remediation-core-design.md`

**Interfaces:**
- Consumes: tout ce qui précède (les textes doivent correspondre aux Global Constraints).
- Produces: documentation définitive.

- [ ] **Step 1: README — section Protocol**

Remplacer la section `## Protocol` par :

```markdown
## Protocol

- Transport: newline-delimited JSON-RPC 2.0 over stdin/stdout, one JSON object
  per line. Batches are rejected (`-32600`); the `jsonrpc` member is tolerated
  when absent.
- Protocol versions: `2024-11-05`, `2025-06-18` and `2025-11-25`. The server
  answers the version the client asks for when it supports it, otherwise the
  highest supported version older than the request (`2025-03-26`, which
  requires batches, gets `2024-11-05`), otherwise `2025-11-25`.
- Implemented: `initialize`, `notifications/initialized`, `ping`, `tools/list`,
  `tools/call`, `notifications/cancelled`.
- Tool calls run concurrently — four built-in calls at a time, plugin tools one
  at a time beside them — so replies may arrive out of order; they carry the
  request id. `ping` and `tools/list` are answered at once, even during a slow
  call. A request whose id is still in flight is ignored.
- `notifications/cancelled`: a call still waiting is skipped; a running call
  goes to its end (a D-Bus call cannot be interrupted) and its result is
  dropped. No reply is sent for a cancelled request.
- Each D-Bus round-trip of `dbus_call` (introspection, then the call) waits at
  most `--call-timeout-ms` (25000 by default) or the call's `timeout_ms`; on
  expiry the tool error names `org.freedesktop.DBus.Error.NoReply`.
- Errors: invalid JSON → `-32700`; a frame that is not one request object, a
  null or non-scalar `id`, a missing `method` → `-32600`; unknown method →
  `-32601`; `tools/call` without `name`, with non-object `arguments`, or for an
  unknown tool → `-32602`. A failed D-Bus call returns an MCP tool error
  (`isError: true`), not a transport error — so an agent can read the message
  and retry.
- Shutdown: when stdin closes (or stdout breaks), results arriving within 2 s
  are still written; the bridge then exits with code 0, even if a call is still
  running.
```

Dans `## Extending: backends and plugins`, après « …registry at startup. The ABI is `org.kde.plasma.mcpbridge.PluginInterface/1.0`. », ajouter :

```markdown
A plugin's tools are called on a worker thread, never on the main thread, and
one at a time (they need not be reentrant). A tool whose name is already
registered is refused with a warning.
```

- [ ] **Step 2: CLAUDE.md**

Remplacer la puce `Server` de `src/mcp/` par :

```markdown
  - `Server` (main thread) validates frames (`-32700`/`-32600`/`-32602`),
    negotiates the protocol version (`mcp/protocolversion.*`), answers
    `initialize`, `ping`, `tools/list` and notifications at once, and hands
    each `tools/call` to `ToolRunner` (`mcp/toolrunner.*`): built-in tools on
    a `QThreadPool` of 4, plugin tools on a pool of 1, results posted back to
    the main thread. Replies may come out of order. On EOF it drains for 2 s,
    then `std::_Exit(0)` if a call still runs (never destroy a pool with a
    running task).
  - `Tool::call()` runs on a worker thread: a tool must not touch
    main-thread-only objects and must not write stdout. `ToolRegistry::add()`
    refuses a name that is already registered.
```

Dans `## CLI flags`, ajouter :

```markdown
- `--call-timeout-ms <ms>` — how long `dbus_call` waits for each D-Bus
  round-trip (default 25000); a call's `timeout_ms` overrides it. Invalid value:
  exit code 2.
```

Dans `## Conventions`, remplacer la puce sur `kDefaultProtocolVersion` par :

```markdown
- The MCP protocol versions the server speaks live in
  `src/mcp/protocolversion.cpp`; the build stamps the package version via the
  `PLASMA_MCP_BRIDGE_VERSION` compile definition (set in `src/CMakeLists.txt`).
```

Dans le paragraphe des tests, ajouter : `` `tests/plugin/testplugin.cpp` is a minimal plugin (sleeping tools, a clashing tool name) built with the tests; `test_async` loads it through `PLASMA_MCP_TEST_PLUGIN`. ``

- [ ] **Step 3: CHANGELOG**

Sous `## 0.2.0 (unreleased)`, ajouter à `### Fixed` :

```markdown
- The server no longer blocks during a slow call: `ping` and `tools/list` are
  answered at once, tool calls run concurrently.
- A client closing stdout no longer kills the bridge with `SIGPIPE`.
- Invalid frames get a JSON-RPC error (`-32700`, `-32600`) instead of being
  dropped with a misleading log line.
```

à `### Changed` :

```markdown
- Replies to `tools/call` may arrive out of order (they carry the request id).
- `initialize` negotiates `2024-11-05`, `2025-06-18` or `2025-11-25` (was
  always `2024-11-05`).
- A tool name registered twice is refused (the first registration wins).
- Plugin tools run on a worker thread, one at a time.
- On stdin EOF the bridge writes the results that arrive within 2 s, then
  exits with code 0 even if a call is still running.
```

à `### Added` :

```markdown
- `notifications/cancelled`; `--call-timeout-ms` and a per-call `timeout_ms`
  argument to `dbus_call`; `DBusBridge::callMethod(…, int timeoutMs)`.
```

- [ ] **Step 4: Spec**

Dans `docs/superpowers/specs/2026-10-01-remediation-core-design.md`, §6, paragraphe **Timeouts**, après « Le timeout borne un appel D-Bus, pas un outil complet. », ajouter : « Décision PR6 : il s'applique à chaque aller-retour, y compris l'introspection qui résout l'interface (pas de timeout d'introspection séparé). »

- [ ] **Step 5: ABI**

Run :
```bash
git worktree add /tmp/pr6-abi-base main
cmake -S /tmp/pr6-abi-base -B /tmp/pr6-abi-base/build -G Ninja -DBUILD_TESTING=OFF >/dev/null && cmake --build /tmp/pr6-abi-base/build >/dev/null
for d in /tmp/pr6-abi-base/build build; do nm -DC --defined-only $d/bin/libplasma-mcp-bridge-core.so | grep -E ' (DBusBridge|ToolRegistry|SkillEmitter)::' | sed 's/^[0-9a-f]* //' | sort > $d.abi; done
diff /tmp/pr6-abi-base/build.abi build.abi; git worktree remove --force /tmp/pr6-abi-base; rm -f build.abi
```
Expected: une seule ligne ajoutée (`> T DBusBridge::callMethod(QString const&, …, QJsonArray const&, int)`), aucune ligne retirée. (Si le garde-fou du worktree refuse ce bloc, placer ces commandes dans un script du workspace et l'exécuter.)

- [ ] **Step 6: Vérifier en local et en Qt 6.4.2**

Run (local) : `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: tous les modules PASS.

Run (Qt 6.4.2 ; un script dans un fichier si l'environnement refuse les `bash -c` en ligne) :
```bash
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src plasma-mcp-ci:noble bash -c '
  cmake -S /src -B /tmp/b -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DPython3_EXECUTABLE=/usr/bin/python3 >/tmp/c.log &&
  cmake --build /tmp/b >/tmp/b.log 2>&1 && grep -c "warning:" /tmp/b.log; ctest --test-dir /tmp/b --output-on-failure --no-tests=error'
```
Expected: `0` warning, tous les modules PASS (dont le stress sous `QT_FATAL_WARNINGS=1`). Lancer deux fois `ctest --test-dir build -R test_async --repeat until-fail:3` en local pour repérer une instabilité de timing ; toute instabilité → superpowers:systematic-debugging, jamais un simple allongement de marge sans cause.

- [ ] **Step 7: Commit**

```bash
git add README.md CLAUDE.md CHANGELOG.md docs/superpowers/specs/2026-10-01-remediation-core-design.md
git commit -s -m "docs: concurrent server, protocol errors and versions, timeouts, shutdown

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Fin de la PR6

- [ ] `git log --oneline main..` montre 8 commits (ce plan + 7 tâches), tous signés ; `git status` propre.
- [ ] Ne pas pousser ni ouvrir la PR sans l'accord de l'utilisateur. La PR (base `main`) liste M4, m3, m8 (partie D), m11, m13, la décision sur le timeout d'introspection, et se termine par `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- [ ] Après merge : bump du sous-module dans l'enterprise avec `skill/SKILL.md` régénéré (`timeout_ms`), et `ctest` de l'enterprise vert (l'outil `atspi_inspect` s'exécute désormais sur un worker du pool de plugins).
