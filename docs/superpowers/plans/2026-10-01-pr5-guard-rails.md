# PR5 — Garde-fous de `dbus_call`, bus validé, `.service` retiré, plugin introuvable fatal — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ajouter au core les garde-fous best-effort du sous-projet C : bus système refusé par défaut, denylist intégrée des appels destructeurs, règles `--deny`/`--allow`/`--default-deny`, refus des noms uniques, une ligne d'audit par `dbus_call` (M5) ; valider `bus` (m5) ; retirer le fichier d'activation D-Bus (m6) ; sortir en code 2 sur un plugin introuvable (m7) ; corriger la documentation (d8, D15) et la description périmée de `dbus_call`.

**Architecture:** Une unité interne `src/core/callpolicy.{h,cpp}` (non installée), sans I/O : options, motifs `SERVICE:INTERFACE.METHOD`, ordre d'évaluation, messages de refus, ligne d'audit. `main.cpp` la construit depuis la ligne de commande et la passe à `DBusBackend`, qui la donne aux trois outils D-Bus. `DBusCallTool::call` résout l'interface manquante par introspection (`resolveMethod`, PR3a), fait juger l'appel, écrit l'audit, puis envoie l'appel **avec l'interface jugée**. `DBusBridge` et `BridgeContext` ne changent pas : les plugins et les appels internes ne sont pas filtrés.

**Tech Stack:** C++17, Qt 6 Core + DBus (≥ 6.4), CMake, tests Python du harnais (PR1) sur bus privé.

**Spec:** `docs/superpowers/specs/2026-10-01-remediation-core-design.md` — §5 (sous-projet C), §2 (M5, m5, m6, m7, d8, D15), §8 (ABI), §9 (changelog), §10 (PR5).

## Global Constraints

- **Base :** branche `remediation/pr5-guard-rails` créée depuis `main` @ `1e3f0e4` (la PR3a est mergée : le résolveur `resolveMethod` existe, le cas « PR5 avant PR3a » de la spec ne se présente pas). Ce plan en est le premier commit.
- **DCO :** tous les commits avec `git commit -s` (identité `fredaime <frederic.aime@gmail.com>`), terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Seule dépendance : Qt 6 Core + DBus. `-Wall -Wextra -Werror` sur Qt 6.4.2 (CI) et Qt 6.11 (local).
- Aucun en-tête **installé** ne change (`core/backend.h`, `core/plugin.h`, `core/skillemitter.h`, `mcp/tool.h`, `mcp/toolregistry.h`, `dbus/dbusbridge.h`) ; IID `org.kde.plasma.mcpbridge.PluginInterface/1.0` inchangé. Les en-têtes touchés ou créés (`core/callpolicy.h`, `dbus/busconnection.h`, `core/pluginloader.h`, `backends/dbusbackend.h`, `tools/dbustools.h`) ne sont pas installés.
- La policy s'applique à l'outil `dbus_call` (et, pour le bus système, aux trois outils D-Bus), jamais à `DBusBridge` ni à `BridgeContext`.
- `--emit-skill` reste déterministe : schémas et descriptions ne dépendent d'aucune option.
- Textes exacts des refus (les tests assertent sur ces sous-chaînes ; tous commencent par `Refused by policy`) :
  - bus système : `Refused by policy: the system bus is disabled (start plasma-mcp-bridge with --allow-system-bus)`
  - nom unique : `Refused by policy: a unique connection name (:N.M) is not accepted as destination; use the service's well-known name (or start plasma-mcp-bridge with --allow-unique-names)`
  - denylist : `Refused by policy: built-in denylist entry <motif> (give 'interface' and start plasma-mcp-bridge with --allow <motif> to permit it)`
  - `--deny` : `Refused by policy: --deny <motif>`
  - `--default-deny` : `Refused by policy: --default-deny is set and no --allow pattern matches`
- Autres messages exacts : `'bus' must be "session" or "system"` ; `invalid --deny pattern '<motif>': expected SERVICE:INTERFACE.METHOD` (idem `--allow`) ; `'service', 'path', 'interface' and 'method' may only contain the characters D-Bus allows in names`.
- Ligne d'audit, format exact (une par `dbus_call` qui atteint la policy, sur stderr, sans passer par le système de log de Qt) : `plasma-mcp-bridge: audit: <allow|deny> <bus> <service> <path> <interface>.<method>[ <règle>]` ; interface inconnue → `*` ; règle ∈ `system-bus`, `unique-name`, `deny:<motif>`, `allow:<motif>`, `builtin:<motif>`, `default-deny`, absente si aucune règle n'a décidé.
- Code de sortie **2** : motif invalide, plugin qui ne se charge pas (mode serveur comme `--emit-skill`).
- Tests : bus privé uniquement (`tests/run_with_bus.sh`) ; aucune fixture ne prend un nom réel hors du bus privé ; assertions sur les noms d'erreur D-Bus, jamais leurs messages (nos messages de policy, eux, sont assertés).
- **Amendements à la spec** (vérifiés par introspection en lecture seule sur Plasma 6.7 / systemd d'Ubuntu 26.10, consignés dans la spec à la Task 6) : la denylist de la spec laissait passer `PowerOffWithFlags`, `RebootWithFlags`, `SuspendWithFlags`…, `Sleep` (logind) et `StartUnitWithFlags`, `StartUnitReplace`, `EnqueueUnitJob`, `KillUnitSubgroup`, `SoftReboot`, `SwitchRoot` et l'interface `org.freedesktop.systemd1.Unit` (systemd). La denylist ci-dessous les couvre par des préfixes (`PowerOff*`…) et des entrées ajoutées. Lecture « la plus stricte » d'une interface inconnue : une règle de refus (`--deny`, denylist) correspond quelle que soit l'interface ; une règle `--allow` ne correspond que si son interface est `*`.
- Conséquence hors dépôt : les descriptions de `dbus_call` et de `bus` changent, donc le `skill/SKILL.md` de l'enterprise dérivera au prochain bump du sous-module (à régénérer à ce moment-là, PR7).

## Review Focus

- **Denylist sur le bus système** : avec `--allow-system-bus`, `PowerOff` sur le bus système reste refusé (la règle ne dépend pas du bus) → `test_denylist_applies_on_the_system_bus` (Task 2).
- **`--default-deny` seul** : la découverte (`dbus_list_services`, `dbus_introspect`) continue de marcher, seul `dbus_call` est refusé → `test_default_deny_leaves_discovery` (Task 3).
- **Motif vide** (`--deny ''`) : erreur de démarrage, code 2, pas une règle qui correspond à tout ou à rien → cas `''` de `test_malformed_patterns_exit_2` (Task 3).
- **Nom avec saut de ligne ou espace** dans `service`/`path`/`interface`/`method` : refusé avant la policy, aucune ligne d'audit forgée → `test_names_cannot_forge_audit_lines` (Task 4).
- **`bus: null`** envoyé par un client pour « non renseigné » : traité comme absent (bus de session), pas comme une erreur → `test_session_bus_is_the_default` (Task 1).

---

## File Structure

| Fichier | Responsabilité |
|---|---|
| `src/core/callpolicy.h/.cpp` (créés) | `CallTarget`, `PolicyDecision`, `CallPolicy` : options, motifs, évaluation, refus, audit. Pur, sans I/O. |
| `src/dbus/busconnection.h` (créé) | `busConnection(name)` : la connexion Qt d'un nom de bus déjà validé (partagée par `DBusBridge` et `DBusCallTool`). |
| `src/dbus/dbusbridge.cpp` (modifié) | `connection()` délègue à `busConnection`. |
| `src/backends/dbusbackend.h/.cpp` (modifiés) | Reçoit `const CallPolicy *` et le passe aux trois outils. |
| `src/tools/dbustools.h/.cpp` (modifiés) | Validation de `bus` (m5), refus du bus système, policy + audit dans `dbus_call`, validation des noms, descriptions. |
| `src/core/pluginloader.h/.cpp` (modifiés) | `load()` signale l'échec (m7). |
| `src/main.cpp` (modifié) | Options `--allow-system-bus`, `--deny`, `--allow`, `--default-deny`, `--allow-unique-names` ; exit 2. |
| `src/CMakeLists.txt` (modifié) | `core/callpolicy.cpp` dans `plasma-mcp-bridge-core`. |
| `CMakeLists.txt`, `data/` (modifié, supprimé) | m6 : plus de `.service`. |
| `tests/fixtures/trap_services.py` (créé) | Doublures des services de la denylist, qui tracent chaque appel reçu. |
| `tests/test_policy.py` (créé) | Bus, denylist, règles, audit, démarrage, install. |
| `tests/test_harness.py`, `tests/test_smoke.py` (modifiés) | `--allow-system-bus` ; descriptions d'outils. |
| `tests/CMakeLists.txt` (modifié) | Ligne `test_policy` ; variables `PLASMA_MCP_BUILD_DIR`, `CMAKE_COMMAND`. |
| `README.md`, `CLAUDE.md`, `CHANGELOG.md`, spec (modifiés) | Sécurité, install (d8), portails (D15), options ; amendements. |

Commandes utiles (depuis la racine du worktree) :

- construire : `cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" && cmake --build build`
- un module : `ctest --test-dir build -R test_policy --output-on-failure`
- une classe : `cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_policy.Buses`
- tout : `ctest --test-dir build --output-on-failure --no-tests=error`

---

### Task 1: m5 et bus système refusé par défaut

**Files:**
- Create: `src/core/callpolicy.h`, `src/core/callpolicy.cpp`, `tests/test_policy.py`
- Modify: `src/CMakeLists.txt`, `src/backends/dbusbackend.h`, `src/backends/dbusbackend.cpp`, `src/tools/dbustools.h`, `src/tools/dbustools.cpp`, `src/main.cpp`, `tests/test_harness.py:27`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `FixtureTestCase`, `ECHO`, `MCPSession.call`/`call_json` (PR1) ; méthode `RetU` de la fixture echo (renvoie `123456789`).
- Produces: `struct PolicyDecision { bool allowed; QString rule; }` ; `class CallPolicy` avec `struct Options { bool allowSystemBus; }`, `bool configure(const Options &, QString *error)`, `bool systemBusAllowed() const`, `static QString refusal(const PolicyDecision &)` ; constructeurs `DBusBackend(const CallPolicy *)`, `DBusListServicesTool/DBusIntrospectTool/DBusCallTool(DBusBridge *, const CallPolicy *)` ; dans `dbustools.cpp` (espace anonyme) `QString busArgument(const QJsonObject &, QString *bus)` et `QString selectBus(const QJsonObject &, const CallPolicy *, QString *bus)` ; en Python `PolicyTestCase.assertRefused(reply, hint)`.

- [ ] **Step 1: Écrire les tests qui échouent**

Créer `tests/test_policy.py` :

```python
# SPDX-License-Identifier: MIT
"""dbus_call guard rails: bus selection, built-in denylist, user rules, audit, startup."""
import unittest

from mcp_session import ECHO, FixtureTestCase


class PolicyTestCase(FixtureTestCase):

    def assertRefused(self, reply, hint):
        self.assertTrue(reply.is_error, reply.text)
        self.assertTrue(reply.text.startswith('Refused by policy'), reply.text)
        self.assertIn(hint, reply.text)


SYSTEM_CALLS = (
    ('dbus_list_services', {'bus': 'system'}),
    ('dbus_introspect', {'bus': 'system', 'service': ECHO['service'], 'path': ECHO['path']}),
    ('dbus_call', dict(ECHO, bus='system', method='RetU')),
)


class Buses(PolicyTestCase):

    def test_system_bus_refused_by_default(self):
        bridge = self.bridge()
        for tool, arguments in SYSTEM_CALLS:
            with self.subTest(tool=tool):
                self.assertRefused(bridge.call(tool, arguments), '--allow-system-bus')

    def test_system_bus_with_flag(self):
        bridge = self.bridge('--allow-system-bus')
        for tool, arguments in SYSTEM_CALLS:
            with self.subTest(tool=tool):
                reply = bridge.call(tool, arguments)
                self.assertFalse(reply.is_error, reply.text)
        self.assertIn(ECHO['service'], bridge.call_json('dbus_list_services', {'bus': 'system'}))

    def test_session_bus_is_the_default(self):
        bridge = self.bridge()
        self.assertEqual(bridge.call('dbus_call', dict(ECHO, method='RetU')), ('123456789', False))
        # A client may send null for "not set".
        self.assertEqual(bridge.call('dbus_call', dict(ECHO, bus=None, method='RetU')),
                         ('123456789', False))

    def test_unknown_bus_is_an_error(self):
        bridge = self.bridge('--allow-system-bus')
        for bus in ('sytem', 'SESSION', '', 5, True, ['session']):
            for tool, arguments in SYSTEM_CALLS:
                with self.subTest(tool=tool, bus=bus):
                    reply = bridge.call(tool, dict(arguments, bus=bus))
                    self.assertTrue(reply.is_error, reply.text)
                    self.assertIn('\'bus\' must be "session" or "system"', reply.text)


if __name__ == '__main__':
    unittest.main()
```

Dans `tests/CMakeLists.txt`, ajouter `test_policy` en dernière ligne de `_tests` :

```cmake
set(_tests
    test_smoke
    test_harness
    test_demarshall
    test_call_path
    test_coercion
    test_policy
)
```

Dans `tests/test_harness.py`, `test_system_bus_is_the_private_bus` doit maintenant autoriser le bus système (ligne 27) :

```python
    def test_system_bus_is_the_private_bus(self):
        names = self.bridge('--allow-system-bus').call_json('dbus_list_services', {'bus': 'system'})
```

- [ ] **Step 2: Vérifier l'échec**

Run: `cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" >/dev/null && cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_policy.Buses test_harness; cd ..`
Expected: `test_system_bus_refused_by_default` FAIL (appel accepté), `test_unknown_bus_is_an_error` FAIL (`sytem` retombe sur la session), `test_system_bus_with_flag` et `test_system_bus_is_the_private_bus` FAIL ou ERROR (option `--allow-system-bus` inconnue : le bridge sort, `MCPTimeout`/`EOFError`), `test_session_bus_is_the_default` PASS.

- [ ] **Step 3: Créer l'unité de policy (version bus seulement)**

`src/core/callpolicy.h` :

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

struct PolicyDecision {
    bool allowed = true;
    // The rule that decided, empty when none matched: "system-bus".
    QString rule;
};

// Best-effort guard rails for the D-Bus tools. NOT a security boundary (see
// README, "Built-in guard rails"): it stops known destructive calls an agent
// may propose, nothing more. Immutable once configured, so it may be read
// from several threads. Internal to the core (not installed).
class CallPolicy
{
public:
    struct Options {
        bool allowSystemBus = false; // --allow-system-bus
    };

    // Replaces the configuration. False, with *error set, when an option is
    // invalid; the previous configuration is then kept.
    bool configure(const Options &options, QString *error);

    bool systemBusAllowed() const;

    // Error text returned to the agent for a refused call.
    static QString refusal(const PolicyDecision &decision);

private:
    bool m_allowSystemBus = false;
};
```

`src/core/callpolicy.cpp` :

```cpp
// SPDX-License-Identifier: MIT
#include "core/callpolicy.h"

bool CallPolicy::configure(const Options &options, QString *error)
{
    Q_UNUSED(error); // no option can be invalid yet
    m_allowSystemBus = options.allowSystemBus;
    return true;
}

bool CallPolicy::systemBusAllowed() const
{
    return m_allowSystemBus;
}

QString CallPolicy::refusal(const PolicyDecision &decision)
{
    Q_UNUSED(decision); // only "system-bus" so far
    return QStringLiteral("Refused by policy: the system bus is disabled "
                          "(start plasma-mcp-bridge with --allow-system-bus)");
}
```

Dans `src/CMakeLists.txt`, ajouter `core/callpolicy.cpp` à `plasma-mcp-bridge-core`, juste avant `core/pluginloader.cpp` :

```cmake
    core/callpolicy.cpp
    core/pluginloader.cpp
```

- [ ] **Step 4: Passer la policy aux outils et valider `bus`**

`src/backends/dbusbackend.h` — remplacer la classe :

```cpp
class CallPolicy;

// Built-in backend exposing the three generic D-Bus tools (list_services,
// introspect, call). This is the universal automation surface: any Plasma or
// freedesktop service can be reached through it, within the CallPolicy.
class DBusBackend : public Backend
{
public:
    explicit DBusBackend(const CallPolicy *policy) : m_policy(policy) {}
    QString name() const override;
    QString description() const override;
    void registerTools(ToolRegistry *registry, const BridgeContext &context) override;

private:
    const CallPolicy *m_policy;
};
```

`src/backends/dbusbackend.cpp` — `registerTools` :

```cpp
void DBusBackend::registerTools(ToolRegistry *registry, const BridgeContext &context)
{
    registry->add(std::make_unique<DBusListServicesTool>(context.dbus, m_policy));
    registry->add(std::make_unique<DBusIntrospectTool>(context.dbus, m_policy));
    registry->add(std::make_unique<DBusCallTool>(context.dbus, m_policy));
}
```

`src/tools/dbustools.h` — ajouter `class CallPolicy;` après `class DBusBridge;`, et pour **chacune** des trois classes remplacer le constructeur et les membres privés :

```cpp
    DBusListServicesTool(DBusBridge *bridge, const CallPolicy *policy)
        : m_bridge(bridge), m_policy(policy) {}
```
```cpp
private:
    DBusBridge *m_bridge;
    const CallPolicy *m_policy;
```

(même chose avec `DBusIntrospectTool(...)` et `DBusCallTool(...)`.)

`src/tools/dbustools.cpp` — ajouter `#include "core/callpolicy.h"` avant `#include "dbus/dbusbridge.h"`, puis dans l'espace anonyme, après `stringify` :

```cpp
// m5: 'bus' is "session" (also when absent or null) or "system"; anything
// else is an error, not a silent fallback to the session bus. Returns the
// error text, empty on success.
QString busArgument(const QJsonObject &arguments, QString *bus)
{
    const QJsonValue value = arguments.value(QStringLiteral("bus"));
    if (value.isUndefined() || value.isNull()) {
        *bus = QStringLiteral("session");
        return QString();
    }
    const QString name = value.toString();
    if (name != QLatin1String("session") && name != QLatin1String("system"))
        return QStringLiteral("'bus' must be \"session\" or \"system\"");
    *bus = name;
    return QString();
}

// busArgument, then the --allow-system-bus switch.
QString selectBus(const QJsonObject &arguments, const CallPolicy *policy, QString *bus)
{
    const QString error = busArgument(arguments, bus);
    if (!error.isEmpty())
        return error;
    if (*bus == QLatin1String("system") && !policy->systemBusAllowed())
        return CallPolicy::refusal(PolicyDecision{false, QStringLiteral("system-bus")});
    return QString();
}
```

Dans les trois `call()`, remplacer la ligne `const QString bus = arguments.value(QStringLiteral("bus")).toString(QStringLiteral("session"));` par :

```cpp
    QString bus;
    const QString busError = selectBus(arguments, m_policy, &bus);
    if (!busError.isEmpty())
        return ToolResult::failure(busError);
```

- [ ] **Step 5: Construire la policy dans `main.cpp`**

Ajouter `#include "core/callpolicy.h"` (après `#include "core/backend.h"`). Remplacer `builtinBackends()` :

```cpp
std::vector<std::unique_ptr<Backend>> builtinBackends(const CallPolicy *policy)
{
    std::vector<std::unique_ptr<Backend>> backends;
    backends.push_back(std::make_unique<DBusBackend>(policy));
    backends.push_back(std::make_unique<NotificationBackend>());
    return backends;
}
```

Après `parser.addOption(emitSkillOption);` :

```cpp
    QCommandLineOption allowSystemBusOption(QStringLiteral("allow-system-bus"),
        QStringLiteral("Let the D-Bus tools reach the system bus (refused by default)."));
    parser.addOption(allowSystemBusOption);
```

Après `parser.process(app);`, avant `DBusBridge bridge;` :

```cpp
    CallPolicy::Options policyOptions;
    policyOptions.allowSystemBus = parser.isSet(allowSystemBusOption);
    CallPolicy policy;
    QString policyError;
    if (!policy.configure(policyOptions, &policyError)) {
        qCritical("plasma-mcp-bridge: %s", qUtf8Printable(policyError));
        return 2;
    }
```

et remplacer `auto allBackends = builtinBackends();` par `auto allBackends = builtinBackends(&policy);`. (`policy` est déclarée avant `registry` : les outils, détruits avec le registre, ne lui survivent pas.)

- [ ] **Step 6: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS, dont `test_policy` (4 tests) et `test_harness`.

- [ ] **Step 7: Commit**

```bash
git add src/core/callpolicy.h src/core/callpolicy.cpp src/CMakeLists.txt src/backends/dbusbackend.h src/backends/dbusbackend.cpp src/tools/dbustools.h src/tools/dbustools.cpp src/main.cpp tests/test_policy.py tests/test_harness.py tests/CMakeLists.txt
git commit -s -m "Refuse the system bus unless --allow-system-bus; reject unknown bus names (M5, m5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Denylist intégrée, noms uniques, interface jugée = interface envoyée

**Files:**
- Create: `tests/fixtures/trap_services.py`, `src/dbus/busconnection.h`
- Modify: `src/core/callpolicy.h`, `src/core/callpolicy.cpp` (remplacés en entier), `src/tools/dbustools.cpp` (`DBusCallTool::call`, includes), `src/dbus/dbusbridge.cpp` (`connection`), `tests/test_policy.py`

**Interfaces:**
- Consumes: `PolicyTestCase.assertRefused`, `busArgument` (Task 1) ; `MethodResolution`, `resolveMethod(const QDBusConnection &, service, path, interface, method)` (PR3a, `dbus/interfaceresolver.h`).
- Produces: `struct CallTarget { QString bus, service, path, interface, method; }` (`interface` vide = inconnue) ; `CallPolicy::CallPolicy()`, `bool destinationAllowed(const QString &bus, const QString &service) const`, `PolicyDecision evaluate(const CallTarget &) const`, `static QStringList builtinDenylist()` ; privés `struct Pattern { QString text, service, interface, method; }`, `static bool parsePattern(const QString &, Pattern *)`, `static bool matches(const Pattern &, const CallTarget &, bool unknownInterfaceMatches)` ; `QDBusConnection busConnection(const QString &busName)` (inline, `dbus/busconnection.h`) ; en Python, depuis `fixtures.trap_services` : `DENIED`, `LOGIN1`, `LOGIN1_PATH`, `LOGIN1_MANAGER`, `SYSTEMD1`, `SYSTEMD1_PATH`, `DECOY`, `HIDDEN_PATH`, `TRAP`, et dans `test_policy.py` la classe `TrapTestCase` (`calls()`, `login1(method, *flags, **extra)`).

- [ ] **Step 1: Créer la fixture**

`tests/fixtures/trap_services.py` :

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stand-ins for the services of the built-in denylist (policy tests).

Owns the bus names of DENIED on the private test bus, on one connection.
Every member records "<path>|<interface>|<member>" and replies a string;
org.plasmamcp.Trap at /Trap returns the record (Calls) and clears it (Reset).
- On /org/freedesktop/login1, PowerOff and Ping also exist on the decoy
  interface org.plasmamcp.Decoy: without 'interface' they are ambiguous.
- /org/freedesktop/login1/hidden answers PowerOff, but its introspection
  data is empty: the interface cannot be resolved.
Prints "READY org.freedesktop.login1" once every name is owned.
"""
import os
import sys

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

LOGIN1 = 'org.freedesktop.login1'
LOGIN1_PATH = '/org/freedesktop/login1'
LOGIN1_MANAGER = 'org.freedesktop.login1.Manager'
SYSTEMD1 = 'org.freedesktop.systemd1'
SYSTEMD1_PATH = '/org/freedesktop/systemd1'
SYSTEMD1_MANAGER = 'org.freedesktop.systemd1.Manager'
DECOY = 'org.plasmamcp.Decoy'
TRAP = 'org.plasmamcp.Trap'
HIDDEN_PATH = LOGIN1_PATH + '/hidden'

# (bus name, object path, interface, members): every member must be refused,
# with and without 'interface'. The wildcard entries of the denylist are
# covered by the concrete members they must catch.
DENIED = (
    (LOGIN1, LOGIN1_PATH, LOGIN1_MANAGER, (
        'PowerOff', 'PowerOffWithFlags', 'Reboot', 'RebootWithFlags', 'Halt', 'HaltWithFlags',
        'Suspend', 'SuspendWithFlags', 'Hibernate', 'HibernateWithFlags', 'HybridSleep',
        'HybridSleepWithFlags', 'SuspendThenHibernate', 'SuspendThenHibernateWithFlags',
        'Sleep', 'TerminateSession', 'TerminateUser', 'TerminateSeat', 'KillSession',
        'KillUser', 'ScheduleShutdown', 'SetWallMessage')),
    ('org.kde.KWin', '/Scripting', 'org.kde.kwin.Scripting', (
        'loadScript', 'loadDeclarativeScript', 'unloadScript', 'start')),
    ('org.kde.ksmserver', '/KSMServer', 'org.kde.KSMServerInterface', ('closeSession', 'logout')),
    ('org.kde.Shutdown', '/Shutdown', 'org.kde.Shutdown', (
        'logout', 'logoutAndShutdown', 'logoutAndReboot')),
    ('org.kde.plasmashell', '/PlasmaShell', 'org.kde.PlasmaShell', ('evaluateScript',)),
    (SYSTEMD1, SYSTEMD1_PATH, SYSTEMD1_MANAGER, (
        'StartUnit', 'StartUnitWithFlags', 'StartUnitReplace', 'StartTransientUnit',
        'RestartUnit', 'ReloadOrRestartUnit', 'EnqueueUnitJob', 'KillUnit', 'KillUnitSubgroup',
        'SetEnvironment', 'UnsetAndSetEnvironment', 'PowerOff', 'Reboot', 'SoftReboot', 'Halt',
        'KExec', 'Exit', 'SwitchRoot')),
    (SYSTEMD1, SYSTEMD1_PATH + '/unit/app_2eservice', 'org.freedesktop.systemd1.Unit', (
        'Start', 'Restart', 'ReloadOrRestart', 'Kill', 'EnqueueJob')),
)

# (object path, interface, member, reply): members the denylist lets through.
HARMLESS = (
    (LOGIN1_PATH, LOGIN1_MANAGER, 'CanPowerOff', 'yes'),
    (LOGIN1_PATH, LOGIN1_MANAGER, 'Ping', 'manager'),
    (LOGIN1_PATH, DECOY, 'Ping', 'decoy'),
    (LOGIN1_PATH, DECOY, 'PowerOff', 'decoy'),
    (SYSTEMD1_PATH, SYSTEMD1_MANAGER, 'GetDefaultTarget', 'graphical.target'),
)

CALLS = []


def _member(interface, name, reply):
    def method(self, msg=None):
        CALLS.append('%s|%s|%s' % (msg.get_path(), msg.get_interface(), msg.get_member()))
        return reply
    method.__name__ = name
    return dbus.service.method(interface, in_signature='', out_signature='s',
                               message_keyword='msg')(method)


def _object_class(path, interfaces):
    """One class per interface, each deriving from the previous: dbus-python
    looks a member up by attribute name, class by class along the MRO, so a
    name declared by two interfaces must live in two classes."""
    cls = dbus.service.Object
    for index, (interface, members) in enumerate(sorted(interfaces.items())):
        namespace = {name: _member(interface, name, reply) for name, reply in members}
        cls = type('Trap%d%s' % (index, path.replace('/', '_')), (cls,), namespace)
    return cls


class Hidden(dbus.service.Object):
    """Answers PowerOff, but its introspection data declares nothing."""

    @dbus.service.method('org.freedesktop.DBus.Introspectable', in_signature='',
                         out_signature='s')
    def Introspect(self):
        return '<node/>'

    PowerOff = _member(LOGIN1_MANAGER, 'PowerOff', 'called')


class Trap(dbus.service.Object):

    @dbus.service.method(TRAP, in_signature='', out_signature='as')
    def Calls(self):
        return CALLS

    @dbus.service.method(TRAP, in_signature='', out_signature='')
    def Reset(self):
        del CALLS[:]


def main():
    if os.environ.get('PLASMA_MCP_TEST_BUS') != os.environ.get('DBUS_SESSION_BUS_ADDRESS'):
        sys.exit('trap_services: refusing to own names outside the private test bus '
                 '(run through tests/run_with_bus.sh)')
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    objects = {}
    for _, path, interface, members in DENIED:
        objects.setdefault(path, {}).setdefault(interface, []).extend(
            (member, 'called') for member in members)
    for path, interface, member, reply in HARMLESS:
        objects.setdefault(path, {}).setdefault(interface, []).append((member, reply))
    keep = [dbus.service.BusName(name, bus, do_not_queue=True)
            for name in sorted({entry[0] for entry in DENIED})]
    keep += [_object_class(path, interfaces)(bus, path)
             for path, interfaces in sorted(objects.items())]
    keep += [Hidden(bus, HIDDEN_PATH), Trap(bus, '/Trap')]
    print('READY ' + LOGIN1, flush=True)
    GLib.MainLoop().run()


if __name__ == '__main__':
    main()
```

- [ ] **Step 2: Écrire les tests qui échouent**

Dans `tests/test_policy.py`, remplacer les imports par :

```python
import unittest

import dbus

from fixtures.trap_services import (DECOY, DENIED, HIDDEN_PATH, LOGIN1, LOGIN1_MANAGER,
                                    LOGIN1_PATH, SYSTEMD1, SYSTEMD1_PATH, TRAP)
from mcp_session import ECHO, FixtureTestCase
```

et ajouter, avant `if __name__ == '__main__':` :

```python
class TrapTestCase(PolicyTestCase):
    fixtures = ('fixtures/trap_services.py',)

    def setUp(self):
        self.trap = dbus.SessionBus().get_object(LOGIN1, '/Trap')
        self.trap.Reset(dbus_interface=TRAP)

    def calls(self):
        """What the stand-ins received since setUp, as "<path>|<interface>|<member>"."""
        return [str(call) for call in self.trap.Calls(dbus_interface=TRAP)]

    def login1(self, method, *flags, **extra):
        """dbus_call on the login1 stand-in from a fresh bridge started with flags."""
        arguments = dict({'service': LOGIN1, 'path': LOGIN1_PATH, 'method': method}, **extra)
        return self.bridge(*flags).call('dbus_call', arguments)


class BuiltinDenylist(TrapTestCase):

    def test_every_denied_member_is_refused(self):
        bridge = self.bridge()
        for service, path, interface, members in DENIED:
            for member in members:
                for given in (interface, None):
                    with self.subTest(service=service, member=member, interface=given):
                        arguments = {'service': service, 'path': path, 'method': member}
                        if given:
                            arguments['interface'] = given
                        self.assertRefused(bridge.call('dbus_call', arguments),
                                           'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_ambiguous_member_is_refused(self):
        # PowerOff is declared by the Manager and by the decoy interface.
        self.assertRefused(self.login1('PowerOff'), 'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_unresolvable_member_is_refused(self):
        self.assertRefused(self.login1('PowerOff', path=HIDDEN_PATH), 'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_same_member_on_another_interface_is_allowed(self):
        self.assertEqual(self.login1('PowerOff', interface=DECOY), ('decoy', False))
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + DECOY + '|PowerOff'])

    def test_harmless_members_are_allowed(self):
        self.assertEqual(self.login1('CanPowerOff'), ('yes', False))
        self.assertEqual(self.login1('CanPowerOff', interface=LOGIN1_MANAGER), ('yes', False))
        reply = self.bridge().call('dbus_call', {
            'service': SYSTEMD1, 'path': SYSTEMD1_PATH, 'method': 'GetDefaultTarget'})
        self.assertEqual(reply, ('graphical.target', False))

    def test_resolved_interface_is_sent(self):
        # Without 'interface', the call carries the interface the policy judged.
        self.login1('CanPowerOff')
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + LOGIN1_MANAGER + '|CanPowerOff'])

    def test_bus_daemon_activation_environment_is_refused(self):
        reply = self.bridge().call('dbus_call', {
            'service': 'org.freedesktop.DBus', 'path': '/org/freedesktop/DBus',
            'interface': 'org.freedesktop.DBus', 'method': 'UpdateActivationEnvironment',
            'args': [{}]})
        self.assertRefused(reply, 'built-in denylist entry')

    def test_denylist_applies_on_the_system_bus(self):
        # The harness's system bus is the private bus: the stand-in is there too.
        reply = self.bridge('--allow-system-bus').call('dbus_call', {
            'bus': 'system', 'service': LOGIN1, 'path': LOGIN1_PATH,
            'interface': LOGIN1_MANAGER, 'method': 'PowerOff'})
        self.assertRefused(reply, 'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_unique_name_destination_is_refused(self):
        owner = str(dbus.SessionBus().get_name_owner(LOGIN1))
        bridge = self.bridge()
        for method in ('PowerOff', 'CanPowerOff'):
            with self.subTest(method=method):
                reply = bridge.call('dbus_call', {'service': owner, 'path': LOGIN1_PATH,
                                                  'interface': LOGIN1_MANAGER, 'method': method})
                self.assertRefused(reply, '--allow-unique-names')
        self.assertEqual(self.calls(), [])
```

- [ ] **Step 3: Vérifier l'échec**

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_policy.BuiltinDenylist; cd ..`
Expected: la fixture démarre (pas de `RuntimeError ... READY`) ; FAIL pour `test_every_denied_member_is_refused`, `test_ambiguous_member_is_refused`, `test_unresolvable_member_is_refused`, `test_bus_daemon_activation_environment_is_refused`, `test_denylist_applies_on_the_system_bus`, `test_unique_name_destination_is_refused` (les appels passent) ; PASS pour `test_same_member_on_another_interface_is_allowed`, `test_harmless_members_are_allowed` et `test_resolved_interface_is_sent` (la PR3a envoie déjà l'interface résolue — ce dernier test protège ce comportement, dont la policy dépend).

- [ ] **Step 4: Partager la sélection de connexion**

Créer `src/dbus/busconnection.h` :

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QDBusConnection>
#include <QString>

// "system" selects the system bus, anything else the session bus. The tools
// validate the name first (m5). Internal to the core (not installed).
inline QDBusConnection busConnection(const QString &busName)
{
    return busName == QLatin1String("system") ? QDBusConnection::systemBus()
                                              : QDBusConnection::sessionBus();
}
```

Dans `src/dbus/dbusbridge.cpp`, ajouter `#include "dbus/busconnection.h"` (avant `#include "dbus/interfaceresolver.h"`) et remplacer le corps de `connection` :

```cpp
QDBusConnection DBusBridge::connection(const QString &busName)
{
    return busConnection(busName);
}
```

- [ ] **Step 5: Remplacer l'unité de policy**

`src/core/callpolicy.h` (fichier entier) :

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// One dbus_call as the policy sees it. `interface` is empty when it is
// unknown: not given, and the introspection data does not name exactly one
// interface declaring the method.
struct CallTarget {
    QString bus;
    QString service;
    QString path;
    QString interface;
    QString method;
};

struct PolicyDecision {
    bool allowed = true;
    // The rule that decided, empty when none matched: "system-bus",
    // "unique-name" or "builtin:<pattern>".
    QString rule;
};

// Best-effort guard rails for the D-Bus tools. NOT a security boundary (see
// README, "Built-in guard rails"): it stops known destructive calls an agent
// may propose, nothing more. Immutable once configured, so it may be read
// from several threads. Internal to the core (not installed).
class CallPolicy
{
public:
    struct Options {
        bool allowSystemBus = false; // --allow-system-bus
    };

    // Defaults: system bus and unique names refused, built-in denylist on.
    CallPolicy();

    // Replaces the configuration. False, with *error set, when an option is
    // invalid; the previous configuration is then kept.
    bool configure(const Options &options, QString *error);

    bool systemBusAllowed() const;
    // False when the bus or the destination alone is refused: the caller
    // then skips resolving the interface (no round-trip for a refused call).
    bool destinationAllowed(const QString &bus, const QString &service) const;
    PolicyDecision evaluate(const CallTarget &target) const;

    // Error text returned to the agent for a refused call.
    static QString refusal(const PolicyDecision &decision);
    // SERVICE:INTERFACE.METHOD patterns; '*' matches any run of characters.
    static QStringList builtinDenylist();

private:
    struct Pattern {
        QString text;
        QString service;
        QString interface;
        QString method;
    };
    static bool parsePattern(const QString &text, Pattern *out);
    static bool matches(const Pattern &pattern, const CallTarget &target,
                        bool unknownInterfaceMatches);

    bool m_allowSystemBus = false;
    QVector<Pattern> m_builtin;
};
```

`src/core/callpolicy.cpp` (fichier entier) :

```cpp
// SPDX-License-Identifier: MIT
#include "core/callpolicy.h"

namespace {

// '*' matches any run of characters, dots included; every other character
// matches itself, case-sensitively.
bool globMatch(const QString &pattern, const QString &text)
{
    qsizetype p = 0;
    qsizetype t = 0;
    qsizetype star = -1;
    qsizetype resume = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern.at(p) == QLatin1Char('*')) {
            star = p++;
            resume = t;
        } else if (p < pattern.size() && pattern.at(p) == text.at(t)) {
            ++p;
            ++t;
        } else if (star >= 0) {
            p = star + 1;
            t = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern.at(p) == QLatin1Char('*'))
        ++p;
    return p == pattern.size();
}

// Known destructive or code-running methods, checked by introspection on
// Plasma 6.7 and systemd (Ubuntu 26.10). Prefix wildcards also catch the
// *WithFlags, *Replace, ... variants.
const char *const kBuiltinDenylist[] = {
    "org.freedesktop.login1:org.freedesktop.login1.Manager.PowerOff*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Reboot*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Halt*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Suspend*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Hibernate*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.HybridSleep*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Sleep*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.KExec*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Terminate*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.KillSession",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.KillUser",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.ScheduleShutdown",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.SetWallMessage",
    "org.kde.KWin:org.kde.kwin.Scripting.*",
    "org.kde.ksmserver:org.kde.KSMServerInterface.closeSession",
    "org.kde.ksmserver:org.kde.KSMServerInterface.logout*",
    "org.kde.Shutdown:org.kde.Shutdown.*",
    "org.kde.plasmashell:org.kde.PlasmaShell.evaluateScript",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.StartUnit*",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.StartTransientUnit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.RestartUnit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.ReloadOrRestartUnit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.EnqueueUnitJob",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.KillUnit*",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.SetEnvironment",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.UnsetAndSetEnvironment",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.PowerOff",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.Reboot",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.SoftReboot",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.Halt",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.KExec",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.Exit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.SwitchRoot",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.Start",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.Restart",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.ReloadOrRestart",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.Kill",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.EnqueueJob",
    "org.freedesktop.DBus:org.freedesktop.DBus.UpdateActivationEnvironment",
};

} // namespace

CallPolicy::CallPolicy()
{
    for (const QString &text : builtinDenylist()) {
        Pattern pattern;
        const bool valid = parsePattern(text, &pattern);
        Q_ASSERT(valid);
        Q_UNUSED(valid);
        m_builtin.append(pattern);
    }
}

bool CallPolicy::configure(const Options &options, QString *error)
{
    Q_UNUSED(error); // no option can be invalid yet
    m_allowSystemBus = options.allowSystemBus;
    return true;
}

bool CallPolicy::systemBusAllowed() const
{
    return m_allowSystemBus;
}

bool CallPolicy::destinationAllowed(const QString &bus, const QString &service) const
{
    if (bus == QLatin1String("system") && !m_allowSystemBus)
        return false;
    return !service.startsWith(QLatin1Char(':'));
}

PolicyDecision CallPolicy::evaluate(const CallTarget &target) const
{
    if (target.bus == QLatin1String("system") && !m_allowSystemBus)
        return {false, QStringLiteral("system-bus")};
    // Otherwise the denylist could be bypassed with the unique name that
    // dbus_list_services shows.
    if (target.service.startsWith(QLatin1Char(':')))
        return {false, QStringLiteral("unique-name")};
    for (const Pattern &pattern : m_builtin) {
        if (matches(pattern, target, true))
            return {false, QStringLiteral("builtin:") + pattern.text};
    }
    return {true, QString()};
}

QString CallPolicy::refusal(const PolicyDecision &decision)
{
    const QString &rule = decision.rule;
    if (rule == QLatin1String("system-bus"))
        return QStringLiteral("Refused by policy: the system bus is disabled "
                              "(start plasma-mcp-bridge with --allow-system-bus)");
    if (rule == QLatin1String("unique-name"))
        return QStringLiteral("Refused by policy: a unique connection name (:N.M) is not "
                              "accepted as destination; use the service's well-known name (or "
                              "start plasma-mcp-bridge with --allow-unique-names)");
    return QStringLiteral("Refused by policy: built-in denylist entry %1 (give 'interface' and "
                          "start plasma-mcp-bridge with --allow %1 to permit it)")
        .arg(rule.mid(8)); // after "builtin:"
}

QStringList CallPolicy::builtinDenylist()
{
    QStringList list;
    for (const char *entry : kBuiltinDenylist)
        list.append(QString::fromLatin1(entry));
    return list;
}

// SERVICE:INTERFACE.METHOD. The last ':' ends the service (a unique name
// starts with ':'), the last '.' starts the method; no part may be empty.
bool CallPolicy::parsePattern(const QString &text, Pattern *out)
{
    const qsizetype colon = text.lastIndexOf(QLatin1Char(':'));
    const qsizetype dot = text.lastIndexOf(QLatin1Char('.'));
    if (colon <= 0 || dot <= colon + 1 || dot == text.size() - 1)
        return false;
    out->text = text;
    out->service = text.left(colon);
    out->interface = text.mid(colon + 1, dot - colon - 1);
    out->method = text.mid(dot + 1);
    return true;
}

// An unknown interface gets the strictest reading: a refusing rule matches it
// whatever its interface, an allowing rule only when its interface is '*'.
bool CallPolicy::matches(const Pattern &pattern, const CallTarget &target,
                         bool unknownInterfaceMatches)
{
    if (!globMatch(pattern.service, target.service) || !globMatch(pattern.method, target.method))
        return false;
    if (target.interface.isEmpty())
        return unknownInterfaceMatches || pattern.interface == QLatin1String("*");
    return globMatch(pattern.interface, target.interface);
}
```

- [ ] **Step 6: Faire juger `dbus_call`**

Dans `src/tools/dbustools.cpp`, les includes du projet deviennent (après `#include "tools/dbustools.h"`) :

```cpp
#include "core/callpolicy.h"
#include "dbus/busconnection.h"
#include "dbus/dbusbridge.h"
#include "dbus/interfaceresolver.h"
```

et remplacer `DBusCallTool::call` en entier :

```cpp
ToolResult DBusCallTool::call(const QJsonObject &arguments)
{
    QString bus;
    const QString busError = busArgument(arguments, &bus);
    if (!busError.isEmpty())
        return ToolResult::failure(busError);
    const QString service = arguments.value(QStringLiteral("service")).toString();
    const QString path = arguments.value(QStringLiteral("path")).toString();
    const QString interface = arguments.value(QStringLiteral("interface")).toString();
    const QString method = arguments.value(QStringLiteral("method")).toString();
    const QJsonArray args = arguments.value(QStringLiteral("args")).toArray();

    if (service.isEmpty() || path.isEmpty() || method.isEmpty())
        return ToolResult::failure(QStringLiteral("'service', 'path' and 'method' are required"));

    // The policy judges the interface the call will carry. Without one, the
    // introspection data may name it; the call is then sent with that
    // interface explicitly, so what is checked is what is sent.
    CallTarget target{bus, service, path, interface, method};
    if (target.interface.isEmpty() && m_policy->destinationAllowed(bus, service)) {
        const MethodResolution resolution =
            resolveMethod(busConnection(bus), service, path, QString(), method);
        if (resolution.state == MethodResolution::Unique)
            target.interface = resolution.interface;
    }
    const PolicyDecision decision = m_policy->evaluate(target);
    if (!decision.allowed)
        return ToolResult::failure(CallPolicy::refusal(decision));

    const DBusResult result =
        m_bridge->callMethod(bus, service, path, target.interface, method, args);
    if (!result.ok)
        return ToolResult::failure(result.error);
    return ToolResult::ok(stringify(result.value));
}
```

(`dbus_list_services` et `dbus_introspect` gardent `selectBus` ; `dbus_call` traite le bus système dans `evaluate`, pour que la Task 4 l'audite.)

- [ ] **Step 7: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_policy` : 13 tests ; `test_call_path` inchangé, dont `test_ambiguous_method_keeps_interface_empty`).

- [ ] **Step 8: Commit**

```bash
git add tests/fixtures/trap_services.py tests/test_policy.py src/dbus/busconnection.h src/dbus/dbusbridge.cpp src/core/callpolicy.h src/core/callpolicy.cpp src/tools/dbustools.cpp
git commit -s -m "Refuse known destructive calls and unique-name destinations in dbus_call (M5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Règles de l'utilisateur

**Files:**
- Modify: `src/core/callpolicy.h`, `src/core/callpolicy.cpp` (remplacés en entier), `src/main.cpp`, `tests/test_policy.py`

**Interfaces:**
- Consumes: `TrapTestCase`, `login1()`, `calls()` (Task 2) ; `CallPolicy` (Task 2).
- Produces: `CallPolicy::Options { bool allowSystemBus, allowUniqueNames, defaultDeny; QStringList deny, allow; }` ; règles `deny:<motif>`, `allow:<motif>`, `default-deny` ; en Python `run_bridge(*args)` et la classe `Startup`.

- [ ] **Step 1: Écrire les tests qui échouent**

Dans `tests/test_policy.py`, compléter les imports :

```python
import os
import subprocess
import unittest

import dbus

from fixtures.trap_services import (DECOY, DENIED, HIDDEN_PATH, LOGIN1, LOGIN1_MANAGER,
                                    LOGIN1_PATH, SYSTEMD1, SYSTEMD1_PATH, TRAP)
from mcp_session import ECHO, FixtureTestCase, require_private_bus
```

et ajouter, avant `if __name__ == '__main__':` :

```python
MANAGER_POWEROFF = LOGIN1 + ':' + LOGIN1_MANAGER + '.PowerOff'
CAN_POWEROFF = LOGIN1 + ':' + LOGIN1_MANAGER + '.CanPowerOff'


class UserRules(TrapTestCase):

    def test_allow_lifts_a_builtin_entry(self):
        reply = self.login1('PowerOff', '--allow', MANAGER_POWEROFF, interface=LOGIN1_MANAGER)
        self.assertEqual(reply, ('called', False))
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + LOGIN1_MANAGER + '|PowerOff'])

    def test_allow_does_not_match_an_unknown_interface(self):
        # Without 'interface', PowerOff is ambiguous: the rule names an interface.
        self.assertRefused(self.login1('PowerOff', '--allow', MANAGER_POWEROFF),
                           'built-in denylist entry')
        self.assertEqual(self.calls(), [])

    def test_allow_with_any_interface_matches_an_unknown_interface(self):
        reply = self.login1('PowerOff', '--allow', LOGIN1 + ':*.PowerOff')
        self.assertFalse(reply.is_error, reply.text)
        self.assertEqual(len(self.calls()), 1)

    def test_deny_beats_allow(self):
        reply = self.login1('CanPowerOff', '--deny', CAN_POWEROFF, '--allow', CAN_POWEROFF)
        self.assertRefused(reply, '--deny ' + CAN_POWEROFF)
        self.assertEqual(self.calls(), [])

    def test_deny_wildcard(self):
        bridge = self.bridge('--deny', LOGIN1 + ':' + LOGIN1_MANAGER + '.Can*')
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'method': 'CanPowerOff'})
        self.assertRefused(reply, '--deny ' + LOGIN1 + ':' + LOGIN1_MANAGER + '.Can*')
        reply = bridge.call('dbus_call', {'service': SYSTEMD1, 'path': SYSTEMD1_PATH,
                                          'method': 'GetDefaultTarget'})
        self.assertEqual(reply, ('graphical.target', False))

    def test_deny_matches_an_unknown_interface(self):
        # Ping is declared by the Manager and by the decoy.
        reply = self.login1('Ping', '--deny', LOGIN1 + ':' + LOGIN1_MANAGER + '.Ping')
        self.assertRefused(reply, '--deny')
        self.assertEqual(self.calls(), [])

    def test_patterns_are_case_sensitive(self):
        reply = self.login1('CanPowerOff', '--deny', LOGIN1 + ':' + LOGIN1_MANAGER + '.canpoweroff')
        self.assertEqual(reply, ('yes', False))

    def test_default_deny_permits_only_allowed_calls(self):
        bridge = self.bridge('--default-deny', '--allow', CAN_POWEROFF)
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'method': 'CanPowerOff'})
        self.assertEqual(reply, ('yes', False))
        reply = bridge.call('dbus_call', {'service': SYSTEMD1, 'path': SYSTEMD1_PATH,
                                          'method': 'GetDefaultTarget'})
        self.assertRefused(reply, '--default-deny')
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'interface': LOGIN1_MANAGER, 'method': 'PowerOff'})
        self.assertRefused(reply, 'built-in denylist entry')
        self.assertEqual(self.calls(), [LOGIN1_PATH + '|' + LOGIN1_MANAGER + '|CanPowerOff'])

    def test_default_deny_leaves_discovery(self):
        bridge = self.bridge('--default-deny')
        self.assertIn(LOGIN1, bridge.call_json('dbus_list_services'))
        reply = bridge.call('dbus_introspect', {'service': LOGIN1, 'path': LOGIN1_PATH})
        self.assertFalse(reply.is_error, reply.text)
        reply = bridge.call('dbus_call', {'service': LOGIN1, 'path': LOGIN1_PATH,
                                          'method': 'CanPowerOff'})
        self.assertRefused(reply, '--default-deny')

    def test_allow_unique_names(self):
        owner = str(dbus.SessionBus().get_name_owner(LOGIN1))
        reply = self.bridge('--allow-unique-names').call('dbus_call', {
            'service': owner, 'path': LOGIN1_PATH, 'interface': LOGIN1_MANAGER,
            'method': 'CanPowerOff'})
        self.assertEqual(reply, ('yes', False))


def run_bridge(*args):
    """Runs the bridge with an empty stdin and returns the CompletedProcess."""
    return subprocess.run([os.environ['PLASMA_MCP_BRIDGE'], *args], stdin=subprocess.DEVNULL,
                          capture_output=True, timeout=10)


class Startup(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        require_private_bus()

    def test_malformed_patterns_exit_2(self):
        for flag in ('--deny', '--allow'):
            for pattern in ('', 'org.kde.KWin', 'org.kde.KWin:loadScript',
                            ':org.kde.kwin.Scripting.loadScript',
                            'org.kde.KWin:org.kde.kwin.Scripting.', 'org.kde.KWin:.loadScript'):
                with self.subTest(flag=flag, pattern=pattern):
                    result = run_bridge(flag, pattern)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertIn(('invalid %s pattern' % flag).encode(), result.stderr)
```

- [ ] **Step 2: Vérifier l'échec**

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_policy.UserRules test_policy.Startup; cd ..`
Expected: tous les tests de `UserRules` en ERROR (`MCPTimeout`/`EOFError` : options inconnues, le bridge sort avant `initialize`) ; `test_malformed_patterns_exit_2` FAIL (`1 != 2`, erreur d'option inconnue de `QCommandLineParser`).

- [ ] **Step 3: Remplacer l'unité de policy**

`src/core/callpolicy.h` (fichier entier) :

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// One dbus_call as the policy sees it. `interface` is empty when it is
// unknown: not given, and the introspection data does not name exactly one
// interface declaring the method.
struct CallTarget {
    QString bus;
    QString service;
    QString path;
    QString interface;
    QString method;
};

struct PolicyDecision {
    bool allowed = true;
    // The rule that decided, empty when none matched: "system-bus",
    // "unique-name", "deny:<pattern>", "allow:<pattern>", "builtin:<pattern>"
    // or "default-deny".
    QString rule;
};

// Best-effort guard rails for the D-Bus tools. NOT a security boundary (see
// README, "Built-in guard rails"): it stops known destructive calls an agent
// may propose, nothing more. Immutable once configured, so it may be read
// from several threads. Internal to the core (not installed).
class CallPolicy
{
public:
    struct Options {
        bool allowSystemBus = false;   // --allow-system-bus
        bool allowUniqueNames = false; // --allow-unique-names
        bool defaultDeny = false;      // --default-deny
        QStringList deny;              // --deny SERVICE:INTERFACE.METHOD
        QStringList allow;             // --allow SERVICE:INTERFACE.METHOD
    };

    // Defaults: system bus and unique names refused, built-in denylist on.
    CallPolicy();

    // Replaces the configuration. False, with *error set, when a pattern is
    // malformed; the previous configuration is then kept.
    bool configure(const Options &options, QString *error);

    bool systemBusAllowed() const;
    // False when the bus or the destination alone is refused: the caller
    // then skips resolving the interface (no round-trip for a refused call).
    bool destinationAllowed(const QString &bus, const QString &service) const;
    // Order: system bus, unique name, --deny, --allow, built-in denylist,
    // --default-deny.
    PolicyDecision evaluate(const CallTarget &target) const;

    // Error text returned to the agent for a refused call.
    static QString refusal(const PolicyDecision &decision);
    // SERVICE:INTERFACE.METHOD patterns; '*' matches any run of characters.
    static QStringList builtinDenylist();

private:
    struct Pattern {
        QString text;
        QString service;
        QString interface;
        QString method;
    };
    static bool parsePattern(const QString &text, Pattern *out);
    static bool matches(const Pattern &pattern, const CallTarget &target,
                        bool unknownInterfaceMatches);

    bool m_allowSystemBus = false;
    bool m_allowUniqueNames = false;
    bool m_defaultDeny = false;
    QVector<Pattern> m_deny;
    QVector<Pattern> m_allow;
    QVector<Pattern> m_builtin;
};
```

Dans `src/core/callpolicy.cpp`, garder l'espace anonyme (`globMatch`, `kBuiltinDenylist`), le constructeur, `systemBusAllowed`, `builtinDenylist`, `parsePattern` et `matches` tels quels, et remplacer `configure`, `destinationAllowed`, `evaluate` et `refusal` par :

```cpp
bool CallPolicy::configure(const Options &options, QString *error)
{
    const auto parseAll = [error](const QStringList &texts, const char *option,
                                  QVector<Pattern> *out) {
        for (const QString &text : texts) {
            Pattern pattern;
            if (!parsePattern(text, &pattern)) {
                *error = QStringLiteral("invalid %1 pattern '%2': expected "
                                        "SERVICE:INTERFACE.METHOD")
                             .arg(QLatin1String(option), text);
                return false;
            }
            out->append(pattern);
        }
        return true;
    };
    QVector<Pattern> deny;
    QVector<Pattern> allow;
    if (!parseAll(options.deny, "--deny", &deny) || !parseAll(options.allow, "--allow", &allow))
        return false;
    m_allowSystemBus = options.allowSystemBus;
    m_allowUniqueNames = options.allowUniqueNames;
    m_defaultDeny = options.defaultDeny;
    m_deny = deny;
    m_allow = allow;
    return true;
}

bool CallPolicy::destinationAllowed(const QString &bus, const QString &service) const
{
    if (bus == QLatin1String("system") && !m_allowSystemBus)
        return false;
    return m_allowUniqueNames || !service.startsWith(QLatin1Char(':'));
}

PolicyDecision CallPolicy::evaluate(const CallTarget &target) const
{
    if (target.bus == QLatin1String("system") && !m_allowSystemBus)
        return {false, QStringLiteral("system-bus")};
    // Otherwise the denylist could be bypassed with the unique name that
    // dbus_list_services shows.
    if (target.service.startsWith(QLatin1Char(':')) && !m_allowUniqueNames)
        return {false, QStringLiteral("unique-name")};
    for (const Pattern &pattern : m_deny) {
        if (matches(pattern, target, true))
            return {false, QStringLiteral("deny:") + pattern.text};
    }
    for (const Pattern &pattern : m_allow) {
        if (matches(pattern, target, false))
            return {true, QStringLiteral("allow:") + pattern.text};
    }
    for (const Pattern &pattern : m_builtin) {
        if (matches(pattern, target, true))
            return {false, QStringLiteral("builtin:") + pattern.text};
    }
    if (m_defaultDeny)
        return {false, QStringLiteral("default-deny")};
    return {true, QString()};
}

QString CallPolicy::refusal(const PolicyDecision &decision)
{
    const QString &rule = decision.rule;
    if (rule == QLatin1String("system-bus"))
        return QStringLiteral("Refused by policy: the system bus is disabled "
                              "(start plasma-mcp-bridge with --allow-system-bus)");
    if (rule == QLatin1String("unique-name"))
        return QStringLiteral("Refused by policy: a unique connection name (:N.M) is not "
                              "accepted as destination; use the service's well-known name (or "
                              "start plasma-mcp-bridge with --allow-unique-names)");
    if (rule == QLatin1String("default-deny"))
        return QStringLiteral("Refused by policy: --default-deny is set and no --allow pattern "
                              "matches");
    if (rule.startsWith(QLatin1String("deny:")))
        return QStringLiteral("Refused by policy: --deny %1").arg(rule.mid(5));
    return QStringLiteral("Refused by policy: built-in denylist entry %1 (give 'interface' and "
                          "start plasma-mcp-bridge with --allow %1 to permit it)")
        .arg(rule.mid(8)); // after "builtin:"
}
```

- [ ] **Step 4: Ajouter les options**

Dans `src/main.cpp`, après l'ajout de `allowSystemBusOption` :

```cpp
    QCommandLineOption denyOption(QStringLiteral("deny"),
        QStringLiteral("Refuse dbus_call to SERVICE:INTERFACE.METHOD ('*' matches any run of "
                       "characters). May be repeated."),
        QStringLiteral("pattern"));
    parser.addOption(denyOption);

    QCommandLineOption allowOption(QStringLiteral("allow"),
        QStringLiteral("Permit dbus_call to SERVICE:INTERFACE.METHOD although the built-in "
                       "denylist refuses it; with --default-deny, the only calls permitted. "
                       "May be repeated."),
        QStringLiteral("pattern"));
    parser.addOption(allowOption);

    QCommandLineOption defaultDenyOption(QStringLiteral("default-deny"),
        QStringLiteral("Refuse every dbus_call that no --allow pattern matches."));
    parser.addOption(defaultDenyOption);

    QCommandLineOption allowUniqueNamesOption(QStringLiteral("allow-unique-names"),
        QStringLiteral("Accept unique connection names (:N.M) as dbus_call destination. "
                       "The denylist does not see through them."));
    parser.addOption(allowUniqueNamesOption);
```

et compléter le remplissage des options de policy :

```cpp
    CallPolicy::Options policyOptions;
    policyOptions.allowSystemBus = parser.isSet(allowSystemBusOption);
    policyOptions.allowUniqueNames = parser.isSet(allowUniqueNamesOption);
    policyOptions.defaultDeny = parser.isSet(defaultDenyOption);
    policyOptions.deny = parser.values(denyOption);
    policyOptions.allow = parser.values(allowOption);
```

- [ ] **Step 5: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_policy` : 24 tests).

- [ ] **Step 6: Commit**

```bash
git add src/core/callpolicy.h src/core/callpolicy.cpp src/main.cpp tests/test_policy.py
git commit -s -m "Add --deny, --allow, --default-deny and --allow-unique-names

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Audit et noms D-Bus validés

**Files:**
- Modify: `src/core/callpolicy.h`, `src/core/callpolicy.cpp` (ajout de `auditLine`), `src/tools/dbustools.cpp` (`DBusCallTool::call`, espace anonyme, includes), `tests/test_policy.py`

**Interfaces:**
- Consumes: `CallTarget`, `PolicyDecision`, `CallPolicy::evaluate` (Task 3) ; `MCPSession.stderr_text()` (PR1).
- Produces: `static QString CallPolicy::auditLine(const CallTarget &, const PolicyDecision &)` ; dans `dbustools.cpp` (espace anonyme) `bool nameCharactersOnly(const QString &, const char *extra)` et `void writeAudit(const QString &line)`.

- [ ] **Step 1: Écrire les tests qui échouent**

Dans `tests/test_policy.py`, ajouter `import re` aux imports, puis avant `if __name__ == '__main__':` :

```python
AUDIT = 'plasma-mcp-bridge: audit: '
AUDIT_LINE = re.compile('^' + re.escape(AUDIT) + '.*$', re.MULTILINE)
RET_VOID = 'org.plasmamcp.Validation /Echo org.plasmamcp.Validation.RetVoid'
ECHO_RULE = 'org.plasmamcp.Validation:*.RetVoid'


class Audit(PolicyTestCase):

    def audited(self, session):
        return AUDIT_LINE.findall(session.stderr_text())

    def test_allowed_call(self):
        session = self.bridge()
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(self.audited(session), [AUDIT + 'allow session ' + RET_VOID])

    def test_resolved_interface(self):
        session = self.bridge()
        session.call('dbus_call', {'service': ECHO['service'], 'path': ECHO['path'],
                                   'method': 'RetVoid'})
        self.assertEqual(self.audited(session), [AUDIT + 'allow session ' + RET_VOID])

    def test_unknown_interface_is_a_star(self):
        session = self.bridge()
        session.call('dbus_call', {'service': ECHO['service'], 'path': ECHO['path'],
                                   'method': 'Dup', 'args': [5]})
        self.assertEqual(self.audited(session),
                         [AUDIT + 'allow session org.plasmamcp.Validation /Echo *.Dup'])

    def test_deny_rule(self):
        session = self.bridge('--deny', ECHO_RULE)
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(self.audited(session),
                         [AUDIT + 'deny session ' + RET_VOID + ' deny:' + ECHO_RULE])

    def test_allow_rule(self):
        session = self.bridge('--allow', ECHO_RULE)
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(self.audited(session),
                         [AUDIT + 'allow session ' + RET_VOID + ' allow:' + ECHO_RULE])

    def test_system_bus_refusal(self):
        session = self.bridge()
        session.call('dbus_call', dict(ECHO, bus='system', method='RetVoid'))
        self.assertEqual(self.audited(session),
                         [AUDIT + 'deny system ' + RET_VOID + ' system-bus'])

    def test_one_line_per_call(self):
        session = self.bridge()
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        session.call('dbus_call', dict(ECHO, method='RetVoid'))
        self.assertEqual(len(self.audited(session)), 2)

    def test_read_only_tools_are_not_audited(self):
        session = self.bridge()
        session.call('dbus_list_services')
        session.call('dbus_introspect', {'service': ECHO['service'], 'path': ECHO['path']})
        self.assertEqual(self.audited(session), [])

    def test_invalid_calls_are_not_audited(self):
        session = self.bridge()
        session.call('dbus_call', {'service': ECHO['service'], 'path': ECHO['path']})
        session.call('dbus_call', dict(ECHO, bus='sytem', method='RetVoid'))
        self.assertEqual(self.audited(session), [])

    def test_names_cannot_forge_audit_lines(self):
        session = self.bridge()
        forged = 'RetVoid\n' + AUDIT + 'allow session x /x x.x'
        for field in ('service', 'path', 'interface', 'method'):
            with self.subTest(field=field):
                arguments = dict(ECHO, method='RetVoid')
                arguments[field] = forged
                reply = session.call('dbus_call', arguments)
                self.assertTrue(reply.is_error, reply.text)
                self.assertIn('may only contain the characters D-Bus allows', reply.text)
        reply = session.call('dbus_call', dict(ECHO, method='Ret Void'))
        self.assertIn('may only contain the characters D-Bus allows', reply.text)
        self.assertEqual(self.audited(session), [])
```

- [ ] **Step 2: Vérifier l'échec**

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_policy.Audit; cd ..`
Expected: FAIL pour `test_allowed_call`, `test_resolved_interface`, `test_unknown_interface_is_a_star`, `test_deny_rule`, `test_allow_rule`, `test_system_bus_refusal`, `test_one_line_per_call` (aucune ligne d'audit) et `test_names_cannot_forge_audit_lines` (message absent) ; PASS pour les deux tests « not audited ».

- [ ] **Step 3: Formater la ligne d'audit**

Dans `src/core/callpolicy.h`, après `refusal` :

```cpp
    // "plasma-mcp-bridge: audit: <allow|deny> <bus> <service> <path>
    // <interface>.<method>[ <rule>]", interface "*" when unknown. The tool
    // has checked that no name contains a space or a line break.
    static QString auditLine(const CallTarget &target, const PolicyDecision &decision);
```

Dans `src/core/callpolicy.cpp`, après `refusal` :

```cpp
QString CallPolicy::auditLine(const CallTarget &target, const PolicyDecision &decision)
{
    QString line = QStringLiteral("plasma-mcp-bridge: audit: %1 %2 %3 %4 %5.%6")
                       .arg(decision.allowed ? QStringLiteral("allow") : QStringLiteral("deny"),
                            target.bus, target.service, target.path,
                            target.interface.isEmpty() ? QStringLiteral("*") : target.interface,
                            target.method);
    if (!decision.rule.isEmpty())
        line += QLatin1Char(' ') + decision.rule;
    return line;
}
```

- [ ] **Step 4: Valider les noms et écrire l'audit**

Dans `src/tools/dbustools.cpp`, ajouter aux includes système :

```cpp
#include <cctype>
#include <cstdio>
#include <cstring>
```

dans l'espace anonyme, après `selectBus` :

```cpp
// Letters, digits, '_' and the characters of `extra`: what D-Bus allows in
// bus names (".-:"), object paths ("/"), interfaces (".") and members ("").
// Checked before the policy and the audit log see a call, so a name can
// neither forge an audit line nor dodge a rule with odd characters.
bool nameCharactersOnly(const QString &text, const char *extra)
{
    for (const QChar c : text) {
        if (c.unicode() >= 128)
            return false;
        const char ch = char(c.unicode());
        if (ch == '\0')
            return false;
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && !std::strchr(extra, ch))
            return false;
    }
    return true;
}

// Straight to stderr, not through Qt's logging: QT_LOGGING_RULES must not be
// able to silence the audit.
void writeAudit(const QString &line)
{
    const QByteArray bytes = line.toUtf8() + '\n';
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    std::fflush(stderr);
}
```

Dans `DBusCallTool::call`, après le test des champs requis :

```cpp
    if (!nameCharactersOnly(service, ".-:") || !nameCharactersOnly(path, "/")
        || !nameCharactersOnly(interface, ".") || !nameCharactersOnly(method, ""))
        return ToolResult::failure(QStringLiteral(
            "'service', 'path', 'interface' and 'method' may only contain the characters D-Bus "
            "allows in names"));
```

et remplacer

```cpp
    const PolicyDecision decision = m_policy->evaluate(target);
    if (!decision.allowed)
```

par

```cpp
    const PolicyDecision decision = m_policy->evaluate(target);
    writeAudit(CallPolicy::auditLine(target, decision));
    if (!decision.allowed)
```

- [ ] **Step 5: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_policy` : 34 tests ; les autres modules ne passent aucun nom invalide).

- [ ] **Step 6: Commit**

```bash
git add src/core/callpolicy.h src/core/callpolicy.cpp src/tools/dbustools.cpp tests/test_policy.py
git commit -s -m "Audit every dbus_call on stderr; reject names D-Bus does not allow

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: m7 (plugin introuvable → exit 2) et m6 (plus de `.service`)

**Files:**
- Modify: `src/core/pluginloader.h`, `src/core/pluginloader.cpp`, `src/main.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/test_policy.py`
- Delete: `data/CMakeLists.txt`, `data/org.kde.plasma.mcpbridge.service.in`

**Interfaces:**
- Consumes: `run_bridge`, `Startup` (Task 3).
- Produces: `bool PluginLoader::load(const QString &path, std::vector<std::unique_ptr<Backend>> *backends)` ; variables d'environnement de test `PLASMA_MCP_BUILD_DIR`, `CMAKE_COMMAND`.

- [ ] **Step 1: Écrire les tests qui échouent**

Dans `tests/test_policy.py`, ajouter `import tempfile` aux imports, puis dans la classe `Startup` :

```python
    def test_missing_plugin_exits_2(self):
        for extra in ((), ('--emit-skill',)):
            with self.subTest(mode=extra or 'server'):
                result = run_bridge('--plugin', '/nonexistent/plugin.so', *extra)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(result.stdout, b'')
                self.assertIn(b'plugin not found', result.stderr)

    def test_file_that_is_not_a_plugin_exits_2(self):
        result = run_bridge('--plugin', os.path.abspath(__file__))
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, b'')
```

et une classe, avant `if __name__ == '__main__':` :

```python
class Install(unittest.TestCase):

    def test_no_dbus_service_file_is_installed(self):
        with tempfile.TemporaryDirectory() as root:
            subprocess.run([os.environ['CMAKE_COMMAND'], '--install',
                            os.environ['PLASMA_MCP_BUILD_DIR']],
                           env=dict(os.environ, DESTDIR=root), check=True,
                           capture_output=True, timeout=60)
            installed = [os.path.relpath(os.path.join(directory, name), root)
                         for directory, _, names in os.walk(root) for name in names]
        self.assertTrue(any(p.endswith('bin/plasma-mcp-bridge') for p in installed), installed)
        self.assertEqual([p for p in installed if p.endswith('.service')], [])
```

Dans `tests/CMakeLists.txt`, compléter l'environnement des tests :

```cmake
    set_tests_properties(${_test} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "PLASMA_MCP_BRIDGE=$<TARGET_FILE:plasma-mcp-bridge>;PYTHONDONTWRITEBYTECODE=1;PLASMA_MCP_BUILD_DIR=${PROJECT_BINARY_DIR};CMAKE_COMMAND=${CMAKE_COMMAND}")
```

- [ ] **Step 2: Vérifier l'échec**

Run: `cmake -S . -B build >/dev/null && cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge PLASMA_MCP_BUILD_DIR=$PWD/../build CMAKE_COMMAND=$(command -v cmake) ./run_with_bus.sh python3 -m unittest -v test_policy.Startup test_policy.Install; cd ..`
Expected: `test_missing_plugin_exits_2` et `test_file_that_is_not_a_plugin_exits_2` FAIL (`0 != 2`, et du JSON sur stdout pour `--emit-skill`) ; `test_no_dbus_service_file_is_installed` FAIL (`share/dbus-1/services/org.kde.plasma.mcpbridge.service` listé) ; `test_malformed_patterns_exit_2` PASS.

- [ ] **Step 3: Rendre l'échec de chargement visible**

`src/core/pluginloader.h` — remplacer la déclaration de `load` :

```cpp
    // Load one plugin and append the backends it contributes to *backends.
    // Returns false when the plugin cannot be used (missing file, not a
    // plugin, wrong IID); the reason is logged to stderr.
    bool load(const QString &path, std::vector<std::unique_ptr<Backend>> *backends);
```

`src/core/pluginloader.cpp` — remplacer `load` :

```cpp
bool PluginLoader::load(const QString &path, std::vector<std::unique_ptr<Backend>> *backends)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        qWarning("plasma-mcp-bridge: plugin not found: %s", qUtf8Printable(path));
        return false;
    }

    auto loader = std::make_unique<QPluginLoader>(info.absoluteFilePath());
    QObject *root = loader->instance();
    if (!root) {
        qWarning("plasma-mcp-bridge: failed to load plugin %s: %s",
                 qUtf8Printable(path), qUtf8Printable(loader->errorString()));
        return false;
    }

    auto *plugin = qobject_cast<PluginInterface *>(root);
    if (!plugin) {
        qWarning("plasma-mcp-bridge: %s does not implement PluginInterface (IID mismatch?)",
                 qUtf8Printable(path));
        loader->unload();
        return false;
    }

    for (auto &backend : plugin->createBackends())
        backends->push_back(std::move(backend));
    m_loaders.push_back(std::move(loader));
    return true;
}
```

`src/main.cpp` — remplacer la boucle de chargement :

```cpp
    PluginLoader loader;
    for (const QString &pluginPath : parser.values(pluginOption)) {
        if (!loader.load(pluginPath, &allBackends)) {
            qCritical("plasma-mcp-bridge: aborting: plugin %s could not be loaded",
                      qUtf8Printable(pluginPath));
            return 2;
        }
    }
```

- [ ] **Step 4: Retirer le fichier d'activation**

```bash
git rm data/CMakeLists.txt data/org.kde.plasma.mcpbridge.service.in
```

Dans `CMakeLists.txt` (racine), supprimer la ligne `add_subdirectory(data)`.

- [ ] **Step 5: Vérifier le succès**

Run: `rm -rf build && cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" >/dev/null && cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: configuration sans référence à `data/` ; build sans warning ; tous les modules PASS (`test_policy` : 37 tests).

- [ ] **Step 6: Commit**

```bash
git add src/core/pluginloader.h src/core/pluginloader.cpp src/main.cpp CMakeLists.txt tests/CMakeLists.txt tests/test_policy.py
git commit -s -m "Exit with code 2 when a plugin cannot be loaded; drop the D-Bus activation file (m6, m7)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Descriptions d'outils, documentation, CHANGELOG, spec, contrôle Qt 6.4.2

**Files:**
- Modify: `src/tools/dbustools.cpp` (`busProperty`, `DBusCallTool::description`, `DBusCallTool::inputSchema`), `tests/test_smoke.py`, `README.md`, `CLAUDE.md`, `CHANGELOG.md`, `docs/superpowers/specs/2026-10-01-remediation-core-design.md`

**Interfaces:**
- Consumes: tout ce qui précède (les textes documentés doivent correspondre aux messages et au format d'audit des Global Constraints).
- Produces: descriptions d'outils définitives (consommées par `--emit-skill` et le `SKILL.md` de l'enterprise).

- [ ] **Step 1: Écrire le test qui échoue**

Dans `tests/test_smoke.py`, ajouter à la classe `Smoke` :

```python
    def test_tool_descriptions_match_the_policy(self):
        tools = {t['name']: t for t in self.bridge().request('tools/list')['result']['tools']}
        call = tools['dbus_call']
        self.assertNotIn('portal', call['description'])
        self.assertIn('--allow', call['description'])
        self.assertNotIn('required for reliable type coercion',
                         call['inputSchema']['properties']['interface']['description'])
        for name in ('dbus_list_services', 'dbus_introspect', 'dbus_call'):
            with self.subTest(tool=name):
                self.assertIn('--allow-system-bus',
                              tools[name]['inputSchema']['properties']['bus']['description'])
```

Run: `cmake --build build && ctest --test-dir build -R test_smoke --output-on-failure`
Expected: FAIL (`'portal'` trouvé dans la description de `dbus_call`).

- [ ] **Step 2: Mettre les descriptions à jour**

Dans `src/tools/dbustools.cpp`, `busProperty()` :

```cpp
    bus.insert(QStringLiteral("description"),
               QStringLiteral("Which bus to use: \"session\" (the default; Plasma lives there) "
                              "or \"system\" (refused unless the bridge was started with "
                              "--allow-system-bus)."));
```

`DBusCallTool::description()` :

```cpp
QString DBusCallTool::description() const
{
    return QStringLiteral(
        "Invoke a method on any D-Bus object and return its reply as JSON. This is the universal "
        "bridge to desktop automation: KWin, plasmashell, global shortcuts, power management, "
        "media players (MPRIS) and any other service on the bus. Arguments are passed "
        "positionally as a JSON array and converted to the types the method declares in its "
        "introspection data: integers are range-checked, a byte array (ay) may be given as a "
        "base64 string, a uint64 beyond the int64 range as a decimal string. A method without "
        "return value replies null. The bridge refuses known destructive methods (power off, "
        "logout, script execution, systemd units), the system bus and unique connection names "
        "(:N.M) unless it was started with the matching --allow option.");
}
```

Dans `DBusCallTool::inputSchema()`, la propriété `interface` :

```cpp
    properties.insert(
        QStringLiteral("interface"),
        stringProperty(QStringLiteral(
            "Interface declaring the method, e.g. org.kde.KWin. May be omitted when exactly one "
            "interface of the object declares the method; give it to choose between several.")));
```

Run: `cmake --build build && ctest --test-dir build -R test_smoke --output-on-failure`
Expected: PASS.

- [ ] **Step 3: README**

1. **Highlights** — remplacer les deux premières puces concernées :

```markdown
- **Universal bridge.** Call any method on any object on the session bus — KWin,
  plasmashell, global shortcuts, power management, media players (MPRIS), and
  more — and on the system bus when you allow it.
```

et, dans la puce « Not just Plasma », `(notifications, portals, logind, MPRIS)` → `(notifications, logind, MPRIS)`.

2. **How it works** — remplacer la phrase sur les standards :

```markdown
Plasma's automation surface lives on the session bus: `org.kde.KWin`,
`org.kde.plasmashell`, `org.kde.kglobalaccel`, `org.kde.ActivityManager`, alongside
cross-desktop standards like `org.freedesktop.Notifications` and
`org.mpris.MediaPlayer2.*`. Because the bridge is a generic D-Bus client, a single
set of tools reaches all of it.
```

3. **Security & trust model** — après le paragraphe « The **Policy / identity / approval** stage is not implemented… », ajouter :

````markdown
### Built-in guard rails (best effort)

The bridge itself adds a few switches and a denylist of known destructive
calls, so that an agent does not power off the machine or run code by
mistake. **This is not a security boundary**: it covers the `dbus_call` tool
only (not the calls plugins or the bridge itself make), it does not see
through other names of the same service or other methods with the same
effect, and it does not filter object paths.

| By default | Switch |
| --- | --- |
| The system bus is refused by the three D-Bus tools | `--allow-system-bus` |
| `dbus_call` to a unique connection name (`:1.42`) is refused — otherwise the denylist could be bypassed with the name `dbus_list_services` shows | `--allow-unique-names` |
| A built-in denylist refuses logind power and session methods (`PowerOff*`, `Reboot*`, `Suspend*`, `Terminate*`, `KillSession`, …), systemd methods that start, kill or reconfigure units, KWin scripting, plasmashell `evaluateScript`, ksmserver `closeSession`, `org.kde.Shutdown`, and the bus daemon's `UpdateActivationEnvironment` | `--allow SERVICE:INTERFACE.METHOD` lifts an entry |
| Everything else is allowed | `--deny SERVICE:INTERFACE.METHOD` refuses more; `--default-deny` makes the `--allow` patterns an allowlist |

Patterns read `SERVICE:INTERFACE.METHOD` (the last `.` starts the method),
`*` matches any run of characters, and matching is case-sensitive; `--deny`
and `--allow` may be repeated. A call is checked in this order: system bus,
unique name, `--deny`, `--allow`, built-in denylist, `--default-deny`. When
`interface` is omitted and the object's introspection data does not name a
single interface for the method, the strictest reading applies: `--deny` and
the denylist match whatever the interface, `--allow` matches only with `*` as
interface.

Every `dbus_call` that reaches these checks writes one line to stderr:

```
plasma-mcp-bridge: audit: <allow|deny> <bus> <service> <path> <interface>.<method> [rule]
```

The interface is `*` when unknown; the rule is `system-bus`, `unique-name`,
`deny:<pattern>`, `allow:<pattern>`, `builtin:<pattern>` or `default-deny`,
and is absent when no rule decided. A malformed pattern, or a `--plugin` that
cannot be loaded, makes the bridge exit with code 2.
````

4. **Build & install** — remplacer `sudo cmake --install build    # installs the binary + a D-Bus service file` par `sudo cmake --install build`, et ajouter après le bloc de code :

```markdown
The install puts the `plasma-mcp-bridge` binary, the shared library
`libplasma-mcp-bridge-core` it runs on, the plugin headers
(`include/plasma-mcp-bridge/`) and the CMake package `PlasmaMcpBridge` for
plugin authors. There is no D-Bus activation file: the MCP client starts the
bridge (see below).
```

5. **Connect it to an agent** — remplacer le dernier paragraphe par :

```markdown
Options such as `--allow-system-bus` or `--deny` go in the client's `args`
list (see [Built-in guard rails](#built-in-guard-rails-best-effort)).

At startup the bridge also claims the well-known name `org.kde.plasma.mcpbridge`
on the session bus, so it shows up in tools like `qdbus` and D-Spy. Only one
running instance holds the name (the others carry on without it), and no
object is exported under it.
```

6. **Usage** — après le paragraphe sur la conversion des arguments, ajouter :

```markdown
XDG desktop portals (`org.freedesktop.portal.*`) are not usable yet: a portal
method returns a request handle and delivers its result later in a `Response`
signal, and the bridge does not receive D-Bus signals.
```

7. **Roadmap** — ajouter en dernière puce : `- Receiving D-Bus signals (portal responses, change notifications)`.

- [ ] **Step 4: CLAUDE.md**

Dans la puce `src/core/`, ajouter un sous-point :

```markdown
  - `CallPolicy` (`core/callpolicy.*`, internal, not installed) holds the guard
    rails of the D-Bus tools: system-bus switch, unique-name refusal, built-in
    denylist, `--deny`/`--allow`/`--default-deny`, audit line format. `main.cpp`
    builds it from the command line and hands it to `DBusBackend`, which passes
    it to the three D-Bus tools. It is not part of `BridgeContext` (ABI):
    `DBusBridge` and plugins are not filtered. `DBusCallTool` resolves a missing
    interface before asking the policy and sends the call with the interface
    that was judged.
```

Remplacer la section `## CLI flags` :

```markdown
## CLI flags

- `--plugin <path>` — load a backend plugin. Repeatable. A plugin that cannot
  be loaded makes the bridge exit with code 2.
- `--emit-skill` — write the deterministic Markdown tool reference (every
  registered tool, including those contributed by `--plugin`) to stdout and exit.
  Used by skill packagers to detect drift between an installed skill and the
  shipping tool surface.
- `--allow-system-bus` — let the D-Bus tools reach the system bus (refused by
  default).
- `--deny SERVICE:INTERFACE.METHOD`, `--allow SERVICE:INTERFACE.METHOD` —
  repeatable `dbus_call` rules (`*` wildcard, case-sensitive); `--allow` lifts a
  built-in denylist entry. A malformed pattern exits with code 2.
- `--default-deny` — only calls matching an `--allow` pattern pass.
- `--allow-unique-names` — accept `:N.M` destinations (the denylist does not
  see through them).
```

Dans le paragraphe des tests, ajouter après la phrase sur `echo_service.py` : `` `trap_services.py` stands in for the services of the built-in denylist (login1, systemd1, KWin, …) on the private bus and records every call it receives, so a policy test asserts that a refused call never arrived. ``

- [ ] **Step 5: CHANGELOG**

Sous `## 0.2.0 (unreleased)`, ajouter à `### Changed` :

```markdown
- The system bus is refused unless the bridge is started with
  `--allow-system-bus` (all three D-Bus tools).
- `dbus_call` refuses a built-in denylist of destructive methods (logind power
  and session methods, systemd unit start/kill/environment, KWin scripting,
  plasmashell `evaluateScript`, ksmserver `closeSession`, `org.kde.Shutdown`,
  `UpdateActivationEnvironment`) and destinations given by unique name
  (`:N.M`). See README, "Built-in guard rails". This is not a security boundary.
- An unknown `bus` value (e.g. `"sytem"`) is an error instead of silently
  meaning the session bus.
- `dbus_call` rejects service, path, interface or method names with characters
  D-Bus does not allow.
- A `--plugin` that cannot be loaded makes the bridge exit with code 2 (also
  with `--emit-skill`).
```

à `### Added` :

```markdown
- `--deny`, `--allow`, `--default-deny`, `--allow-unique-names`; one audit line
  per `dbus_call` on stderr.
```

et une section après `### Added` :

```markdown
### Removed
- The D-Bus activation file `org.kde.plasma.mcpbridge.service`: activating the
  bridge started a stdio server with no client. Packagers: drop it from file
  lists.
```

- [ ] **Step 6: Amender la spec**

Dans `docs/superpowers/specs/2026-10-01-remediation-core-design.md`, §5 :

- remplacer la puce `org.freedesktop.login1:…{PowerOff,Reboot,Halt,Suspend,…}` par :
  ``- `org.freedesktop.login1:org.freedesktop.login1.Manager.{PowerOff*,Reboot*,Halt*,Suspend*,Hibernate*,HybridSleep*,Sleep*,KExec*,Terminate*,KillSession,KillUser,ScheduleShutdown,SetWallMessage}` (préfixes : variantes `*WithFlags` et `Sleep` — amendement PR5)``
- remplacer la puce `org.freedesktop.systemd1:…` par :
  ``- `org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.{StartUnit*,StartTransientUnit,RestartUnit,ReloadOrRestartUnit,EnqueueUnitJob,KillUnit*,SetEnvironment,UnsetAndSetEnvironment,PowerOff,Reboot,SoftReboot,Halt,KExec,Exit,SwitchRoot}` et `org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.{Start,Restart,ReloadOrRestart,Kill,EnqueueJob}` (exécution de commandes via `systemd --user` ; `StartUnitWithFlags`, `StartUnitReplace`, `EnqueueUnitJob`, `KillUnitSubgroup`, `SoftReboot`, `SwitchRoot` et l'interface `Unit` — amendement PR5)``
- dans l'étape 2 de l'**Évaluation**, après « (la plus stricte) », ajouter : « : une règle de refus correspond quelle que soit l'interface, une règle `--allow` seulement si son interface est `*` ».
- après le paragraphe **Audit**, ajouter : « Les noms (`service`, `path`, `interface`, `method`) ne contenant pas que des caractères admis par D-Bus sont refusés avant la policy (amendement PR5 : une ligne d'audit ne peut pas être forgée). `bus: null` vaut `bus` absent. »

- [ ] **Step 7: Vérifier en local et en Qt 6.4.2**

Run (local) : `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error && ./build/bin/plasma-mcp-bridge --emit-skill | grep -c portal`
Expected: tous les modules PASS ; `0` (aucune mention de portail dans la référence des outils).

Run (Qt 6.4.2 ; un script dans un fichier si l'environnement refuse les `bash -c` en ligne) :
```bash
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src plasma-mcp-ci:noble bash -c '
  cmake -S /src -B /tmp/b -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DPython3_EXECUTABLE=/usr/bin/python3 >/tmp/c.log &&
  cmake --build /tmp/b >/tmp/b.log 2>&1 && grep -c "warning:" /tmp/b.log; ctest --test-dir /tmp/b --output-on-failure --no-tests=error'
```
Expected: `0` warning, tous les modules PASS. (Image `plasma-mcp-ci:noble` construite pendant la PR2 ; sinon la reconstruire depuis `ubuntu:24.04` + `build-essential cmake ninja-build extra-cmake-modules qt6-base-dev qt6-base-dev-tools dbus-daemon python3-dbus python3-gi git`.)

- [ ] **Step 8: Commit**

```bash
git add src/tools/dbustools.cpp tests/test_smoke.py README.md CLAUDE.md CHANGELOG.md docs/superpowers/specs/2026-10-01-remediation-core-design.md
git commit -s -m "docs: guard rails, install contents, portals; current dbus_call description (d8, D15)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Fin de la PR5

- [ ] `git log --oneline main..` montre 7 commits (ce plan + 6 tâches), tous signés (`git log --format=%B main.. | grep -c Signed-off-by` = 7) ; `git status` propre.
- [ ] Ne pas pousser ni ouvrir la PR sans l'accord de l'utilisateur. La PR (base `main`) liste M5, m5, m6, m7, d8, D15 et les amendements à la spec, et se termine par `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- [ ] Après merge : la PR6 (serveur concurrent) se rebase sur ce `main` (conflits attendus sur `main.cpp` et `dbustools.cpp`) ; au prochain bump du sous-module dans l'enterprise, régénérer `skill/SKILL.md` (descriptions de `dbus_call` et `bus` modifiées).
