# PR3a — Nombres exacts, chemin d'appel unique, résolveur d'interface, plages scalaires — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre exacts les nombres renvoyés par `dbus_call` (M1), faire passer tous les appels par `QDBusMessage::createMethodCall` avec une interface résolue par introspection (m1, m4), et convertir strictement les arguments scalaires, `as` et `ay` d'après la signature déclarée (M6).

**Architecture:** Deux nouvelles unités internes au core (en-têtes non installés) : `src/dbus/interfaceresolver.{h,cpp}` (introspection + `QXmlStreamReader` → interface et signature d'entrée, trois états) et `src/dbus/typecoercer.{h,cpp}` (conversion stricte d'un argument JSON vers le type D-Bus déclaré ; types basiques, `as`, `ay` ; le reste reste « lâche » jusqu'à la PR3b). `DBusBridge::callMethod` n'utilise plus `QDBusInterface`.

**Tech Stack:** C++17, Qt 6 Core + DBus (≥ 6.4), CMake, tests Python du harnais (PR1).

**Spec:** `docs/superpowers/specs/2026-10-01-remediation-core-design.md` — §4.2 (A1), §9 (changelog), §10 (PR3a), §2 (M1, m1, m4, M6, d5, d9).

## Global Constraints

- **Base :** la PR1 n'est pas encore mergée sur GitHub. Créer la branche `remediation/pr3a-call-path` depuis `remediation/pr1-harness-b1` (`b27552b`) ; la rebaser sur `main` une fois la PR1 mergée.
- **DCO :** le dépôt exige `Signed-off-by` sur chaque commit → tous les commits avec `git commit -s` (identité `fredaime <frederic.aime@gmail.com>`, accord donné par l'utilisateur pour ce dépôt).
- Seule dépendance : Qt 6 Core + DBus (`QXmlStreamReader` est dans QtCore). Aucune dépendance nouvelle.
- `-Wall -Wextra -Werror` sur Qt 6.4.2 (CI) et Qt 6.11 (local).
- Aucun en-tête **installé** ne change (`dbus/dbusbridge.h`, `mcp/*.h`, `core/*.h`) ; IID `PluginInterface/1.0` inchangé ; signature de `DBusBridge::callMethod` inchangée. Pas de cache d'introspection (spec K3) : un `Introspect` par appel.
- Les nouvelles unités portent `// SPDX-License-Identifier: MIT`.
- Pas de garde-fou de policy ici (PR5), pas de conteneurs autres que `as`/`ay`/`a{sv}`, pas de `v`, pas de `h` (PR3b).
- Tests : harnais de la PR1 (`tests/mcp_session.py`, `run_with_bus.sh`), bus privé uniquement ; assertions sur les noms d'erreur D-Bus, jamais leurs messages.
- Messages d'erreur de coercition, format exact : `argument <index> (<signature>): <motif>`, motifs `<valeur> out of range [<min>,<max>]`, `<valeur> is not an integer`, `<valeur> is beyond the int64 range, where JSON numbers lose digits: pass it as a decimal string`, `expected an integer, got <valeur>`, `expected a boolean, got <valeur>`, `expected a number, got <valeur>`, `expected a string, got <valeur>`, `<valeur> is not a valid object path`, `expected an array of strings, got <valeur>`, `expected an array of bytes or a base64 string, got <valeur>`, `<valeur> is not valid base64`. `<valeur>` = la valeur JSON compacte (une chaîne garde ses guillemets).
- Commits terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

- **Méthode `void`** appelée avec ou sans `interface` : `null`, jamais `<unrepresentable:>` → `test_void_with_interface_is_null` (Task 2).
- **Chemin d'objet inexistant** : l'introspection échoue, l'appel part quand même et l'erreur distante remonte, sans plantage → `test_unknown_object_path_reports_remote_error` (Task 2).
- **Entier JSON écrit en réel** (`5.0` pour `u`) : accepté comme `5` → `test_integral_real_accepted` (Task 3).
- **Grand entier** : exact en nombre JSON tant qu'il tient dans un `int64` (Qt garde ces entiers exacts) ; au-delà (grand `uint64`), refusé en nombre et accepté en chaîne décimale → `test_big_integer_number_exact`, `test_uint64_beyond_int64_needs_a_string`, `test_big_integers_as_strings` (Task 3).
- **Booléen passé pour un entier** : erreur explicite, pas de conversion `true → 1` → cas `bool for u` de `test_rejections` (Task 3).

---

## File Structure

| Fichier | Responsabilité |
|---|---|
| `src/tools/dbustools.cpp` (modifié) | `stringify` : nombres scalaires formatés comme dans un document JSON. |
| `src/dbus/dbusbridge.cpp` (modifié) | `variantToJson` (uint64 > 2^53 → chaîne) ; `callMethod` réécrit ; suppression de `coerceToMethodSignature`. |
| `src/dbus/interfaceresolver.h/.cpp` (créés) | `MethodResolution`, `resolveMethodFromXml`, `resolveMethod`. |
| `src/dbus/typecoercer.h/.cpp` (créés) | `TypeCoercer::coerce`. |
| `src/CMakeLists.txt` (modifié) | Ajout des deux `.cpp` à `plasma-mcp-bridge-core` (pas à l'install des en-têtes). |
| `tests/fixtures/echo_service.py` (modifié) | `RetASVBigT`, `EchoIface`, `Dup` dans deux interfaces, `EchoT`, `EchoS`. |
| `tests/test_demarshall.py` (modifié) | Nombres exacts (M1). |
| `tests/test_call_path.py` (créé) | Résolution d'interface, void, démon de bus, erreurs distantes. |
| `tests/test_coercion.py` (créé) | Coercition stricte des scalaires, `as`, `ay`. |
| `tests/CMakeLists.txt` (modifié) | Deux lignes dans `_tests`. |
| `README.md`, `CLAUDE.md`, `CHANGELOG.md` (modifiés) | d5, d9 ; architecture ; changements visibles. |

---

### Task 1: M1 — nombres exacts dans les réponses

**Files:**
- Modify: `tests/fixtures/echo_service.py`, `tests/test_demarshall.py`, `src/tools/dbustools.cpp` (`stringify`), `src/dbus/dbusbridge.cpp` (`variantToJson`, branche `QMetaType::ULongLong`)

**Interfaces:**
- Consumes: `FixtureTestCase`, `ECHO`, `MCPSession.call`, `MCPSession.call_json` (PR1).
- Produces: comportement de sortie — entiers exacts, réels au plus court aller-retour, `uint64` > 2^53 émis en **chaîne** décimale partout.

- [ ] **Step 1: Créer la branche**

```bash
git switch -c remediation/pr3a-call-path b27552b
```

- [ ] **Step 2: Fixture** — dans `tests/fixtures/echo_service.py`, après la méthode `RetASV`, ajouter :

```python
    @dbus.service.method(IFACE, in_signature='', out_signature='a{sv}')
    def RetASVBigT(self):
        return {'big': dbus.UInt64(18446744073709551615, variant_level=1),
                'small': dbus.UInt64(5, variant_level=1)}
```

- [ ] **Step 3: Tests** — dans `tests/test_demarshall.py`, ajouter à la classe `Demarshall` :

```python
    # --- M1: exact numbers ------------------------------------------------
    def scalar(self, method):
        return self.bridge().call('dbus_call', dict(ECHO, method=method))

    def test_uint32_is_exact(self):
        self.assertEqual(self.scalar('RetU'), ('123456789', False))

    def test_int64_is_exact(self):
        self.assertEqual(self.scalar('RetX'), ('4000000', False))

    def test_double_keeps_its_digits(self):
        self.assertEqual(self.scalar('RetD'), ('3.14159265', False))

    def test_uint64_beyond_2_53_is_a_decimal_string(self):
        self.assertEqual(self.scalar('RetT'), ('18446744073709551615', False))
        self.assertEqual(self.ret('RetASVBigT'), {'big': '18446744073709551615', 'small': 5})
```

- [ ] **Step 4: Vérifier l'échec**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R test_demarshall`
Expected: FAIL — `'1.23457e+08'`, `'4e+06'`, `'3.14159'`, `'1.84467e+19'` et `{'big': 1.8446744073709552e+19, …}`.

- [ ] **Step 5: `stringify`** — dans `src/tools/dbustools.cpp`, remplacer :

```cpp
    case QJsonValue::Double:
        return QString::number(value.toDouble());
```

par :

```cpp
    case QJsonValue::Double: {
        // Format a scalar exactly like a number nested in an array or object:
        // integers in full, reals as the shortest round-trip representation.
        const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
        return QString::fromUtf8(json.mid(1, json.size() - 2));
    }
```

- [ ] **Step 6: `variantToJson`** — dans `src/dbus/dbusbridge.cpp`, remplacer :

```cpp
    case QMetaType::ULongLong: {
        // A uint64 above INT64_MAX would wrap to a negative number if cast
        // straight to qint64, so fall back to double for those (lossy, but
        // it preserves sign and magnitude).
        const qulonglong u = value.toULongLong();
        if (u <= static_cast<qulonglong>(std::numeric_limits<qint64>::max()))
            return static_cast<qint64>(u);
        return static_cast<double>(u);
    }
```

par :

```cpp
    case QMetaType::ULongLong: {
        // Beyond 2^53 a JSON number no longer holds the value exactly (clients
        // parse numbers as doubles): emit the exact decimal string instead.
        const qulonglong u = value.toULongLong();
        if (u <= (Q_UINT64_C(1) << 53))
            return static_cast<qint64>(u);
        return QString::number(u);
    }
```

- [ ] **Step 7: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS (tous les modules) ; build sans warning. Si `-Werror` signale `<limits>` inutilisé, ne rien retirer : `std::numeric_limits` reste utilisé ailleurs dans le fichier ; en cas de warning réel, supprimer l'include devenu inutile.

- [ ] **Step 8: Commit**

```bash
git add tests/fixtures/echo_service.py tests/test_demarshall.py src/tools/dbustools.cpp src/dbus/dbusbridge.cpp
git commit -s -m "fix(dbus): exact numbers in replies (M1)

Scalars were formatted with QString::number(double) (6 significant
digits); format them like nested JSON numbers. A uint64 above 2^53 is
emitted as an exact decimal string.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Chemin d'appel unique et résolveur d'interface (m1, m4)

**Files:**
- Create: `src/dbus/interfaceresolver.h`, `src/dbus/interfaceresolver.cpp`, `tests/test_call_path.py`
- Modify: `src/CMakeLists.txt`, `src/dbus/dbusbridge.cpp` (`callMethod`, suppression de `coerceToMethodSignature`), `tests/fixtures/echo_service.py`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: harnais PR1.
- Produces (utilisés par Task 3, PR3b et PR5) :
  - `struct MethodResolution { enum State { Unavailable, Unique, Ambiguous }; State state; QString interface; QStringList inSignature; };`
  - `MethodResolution resolveMethodFromXml(const QString &xml, const QString &interface, const QString &method);`
  - `MethodResolution resolveMethod(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, const QString &method);`
  - Sémantique : `Unique` → `interface` = interface déclarante, `inSignature` = un type complet par argument d'entrée ; `Ambiguous`/`Unavailable` → `interface` = celle fournie par l'appelant (éventuellement vide), `inSignature` vide.

- [ ] **Step 1: Fixture** — dans `tests/fixtures/echo_service.py`, ajouter avant `class Echo` :

```python
def _dup(interface):
    """Same member name in two interfaces: calls without an interface are ambiguous."""
    def Dup(self, a, msg=None):
        return '%s|%s|%r' % (msg.get_interface(), msg.get_signature(), [a])
    return dbus.service.method(interface, in_signature='u', out_signature='s',
                               message_keyword='msg')(Dup)
```

et dans `class Echo`, après les méthodes `Echo*` :

```python
    @dbus.service.method(IFACE, in_signature='u', out_signature='s', message_keyword='msg')
    def EchoIface(self, a, msg=None):
        return '%s|%s|%r' % (msg.get_interface(), msg.get_signature(), [a])

    Dup = _dup(IFACE)
    DupOther = _dup('org.plasmamcp.Other')
```

- [ ] **Step 2: Tests** — créer `tests/test_call_path.py` :

```python
# SPDX-License-Identifier: MIT
"""How dbus_call builds the message: interface resolution, void replies, errors."""
import unittest

from mcp_session import ECHO, FixtureTestCase

NO_IFACE = {'service': ECHO['service'], 'path': ECHO['path']}


class CallPath(FixtureTestCase):

    def call(self, **args):
        return self.bridge().call('dbus_call', args)

    def test_unique_method_gets_its_interface(self):
        reply = self.call(**NO_IFACE, method='EchoIface', args=[5])
        self.assertFalse(reply.is_error, reply.text)
        self.assertTrue(reply.text.startswith('org.plasmamcp.Validation|'), reply.text)

    def test_ambiguous_method_keeps_interface_empty(self):
        reply = self.call(**NO_IFACE, method='Dup', args=[5])
        self.assertFalse(reply.is_error, reply.text)
        self.assertTrue(reply.text.startswith('None|'), reply.text)

    def test_given_interface_resolves_ambiguity(self):
        reply = self.call(**NO_IFACE, interface='org.plasmamcp.Other', method='Dup', args=[5])
        self.assertTrue(reply.text.startswith('org.plasmamcp.Other|'), reply.text)

    def test_void_with_interface_is_null(self):
        self.assertEqual(self.call(**ECHO, method='RetVoid'), ('null', False))

    def test_void_without_interface_is_null(self):
        self.assertEqual(self.call(**NO_IFACE, method='RetVoid'), ('null', False))

    def test_bus_daemon_with_its_interface(self):
        reply = self.call(service='org.freedesktop.DBus', path='/org/freedesktop/DBus',
                          interface='org.freedesktop.DBus', method='GetConnectionUnixProcessID',
                          args=['org.plasmamcp.Validation'])
        self.assertFalse(reply.is_error, reply.text)
        self.assertTrue(reply.text.isdigit(), reply.text)

    def test_unknown_method_reports_remote_error(self):
        reply = self.call(**ECHO, method='NoSuchMethod')
        self.assertTrue(reply.is_error)
        self.assertIn('org.freedesktop.DBus.Error.UnknownMethod', reply.text)

    def test_unknown_object_path_reports_remote_error(self):
        reply = self.call(service=ECHO['service'], path='/NoSuchObject', method='RetU')
        self.assertTrue(reply.is_error)
        self.assertIn('org.freedesktop.DBus.Error.', reply.text)


if __name__ == '__main__':
    unittest.main()
```

Ajouter `test_call_path` à la liste `_tests` de `tests/CMakeLists.txt` (une ligne).

- [ ] **Step 3: Vérifier l'échec**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R test_call_path`
Expected: FAIL — `test_unique_method_gets_its_interface` (`None|x|…` : pas d'interface sans `interface`), `test_void_with_interface_is_null` (`<unrepresentable:>`), `test_bus_daemon_with_its_interface` (`No such interface org.freedesktop.DBus …`). Les autres passent.

- [ ] **Step 4: Créer `src/dbus/interfaceresolver.h`**

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>

class QDBusConnection;

// Which interface and input signature a method call targets, according to
// the object's introspection data. Internal to the core (not installed).
struct MethodResolution {
    enum State {
        Unavailable, // introspection failed, or the method is not declared
        Unique,      // exactly one interface declares the method
        Ambiguous,   // no interface given and several interfaces declare it
    };
    State state = Unavailable;
    // Unique: the declaring interface. Otherwise: the caller's interface,
    // possibly empty.
    QString interface;
    // Unique only: one complete D-Bus type per input argument.
    QStringList inSignature;
};

// Pure parser, used by resolveMethod. `interface` may be empty.
MethodResolution resolveMethodFromXml(const QString &xml, const QString &interface,
                                      const QString &method);

// Introspects service/path (one round-trip, no cache) and resolves `method`.
MethodResolution resolveMethod(const QDBusConnection &bus, const QString &service,
                               const QString &path, const QString &interface,
                               const QString &method);
```

- [ ] **Step 5: Créer `src/dbus/interfaceresolver.cpp`**

```cpp
// SPDX-License-Identifier: MIT
#include "dbus/interfaceresolver.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QList>
#include <QXmlStreamReader>

MethodResolution resolveMethodFromXml(const QString &xml, const QString &interface,
                                      const QString &method)
{
    QList<MethodResolution> candidates; // one per interface declaring `method`
    MethodResolution current;
    QString currentInterface;
    bool inMethod = false;

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QXmlStreamAttributes attributes = reader.attributes();
            if (reader.name() == QLatin1String("interface")) {
                currentInterface = attributes.value(QLatin1String("name")).toString();
            } else if (reader.name() == QLatin1String("method")
                       && attributes.value(QLatin1String("name")) == method
                       && (interface.isEmpty() || currentInterface == interface)) {
                inMethod = true;
                current = MethodResolution{};
                current.state = MethodResolution::Unique;
                current.interface = currentInterface;
            } else if (inMethod && reader.name() == QLatin1String("arg")) {
                // Method arguments default to direction "in".
                const auto direction = attributes.value(QLatin1String("direction"));
                if (direction.isEmpty() || direction == QLatin1String("in"))
                    current.inSignature.append(attributes.value(QLatin1String("type")).toString());
            }
        } else if (reader.isEndElement()) {
            if (inMethod && reader.name() == QLatin1String("method")) {
                candidates.append(current);
                inMethod = false;
            } else if (reader.name() == QLatin1String("interface")) {
                currentInterface.clear();
            }
        }
    }

    MethodResolution result;
    result.interface = interface;
    if (reader.hasError() || candidates.isEmpty())
        return result; // Unavailable
    if (candidates.size() > 1) {
        result.state = MethodResolution::Ambiguous;
        return result;
    }
    return candidates.first();
}

MethodResolution resolveMethod(const QDBusConnection &bus, const QString &service,
                               const QString &path, const QString &interface,
                               const QString &method)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(
        service, path, QStringLiteral("org.freedesktop.DBus.Introspectable"),
        QStringLiteral("Introspect"));
    const QDBusMessage reply = bus.call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        MethodResolution result;
        result.interface = interface;
        return result; // Unavailable
    }
    return resolveMethodFromXml(reply.arguments().first().toString(), interface, method);
}
```

- [ ] **Step 6: CMake** — dans `src/CMakeLists.txt`, ajouter à la liste de `add_library(plasma-mcp-bridge-core SHARED …)`, après `dbus/dbusbridge.cpp` :

```cmake
    dbus/interfaceresolver.cpp
```

- [ ] **Step 7: Réécrire `callMethod`** — dans `src/dbus/dbusbridge.cpp` : ajouter `#include "dbus/interfaceresolver.h"` après `#include "dbus/dbusbridge.h"` ; **supprimer** la fonction `coerceToMethodSignature` (commentaire compris) et l'include `<QMetaMethod>` ; remplacer le corps de `DBusBridge::callMethod` par :

```cpp
DBusResult DBusBridge::callMethod(const QString &busName, const QString &service,
                                  const QString &path, const QString &interface,
                                  const QString &method, const QJsonArray &args)
{
    QDBusConnection bus = connection(busName);
    if (!bus.isConnected())
        return DBusResult::failure(QStringLiteral("Not connected to the %1 bus").arg(busName));

    const MethodResolution resolution = resolveMethod(bus, service, path, interface, method);

    QVariantList variantArgs;
    variantArgs.reserve(args.size());
    for (const QJsonValue &arg : args)
        variantArgs.append(jsonToVariant(arg));

    // A plain method call with an explicit interface. QDBusInterface is not
    // used: its isValid() relies on name-owner tracking, which the bus daemon
    // does not have ("No such interface org.freedesktop.DBus"), and it turns
    // a void reply into an invalid QVariant.
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, resolution.interface, method);
    call.setArguments(variantArgs);
    const QDBusMessage reply = bus.call(call, QDBus::Block);

    if (reply.type() == QDBusMessage::ErrorMessage)
        return DBusResult::failure(
            QStringLiteral("%1: %2").arg(reply.errorName(), reply.errorMessage()));

    const QVariantList out = reply.arguments();
    if (out.isEmpty())
        return DBusResult::success(QJsonValue::Null);
    if (out.size() == 1)
        return DBusResult::success(variantToJson(out.first()));

    QJsonArray array;
    for (const QVariant &value : out)
        array.append(variantToJson(value));
    return DBusResult::success(array);
}
```

- [ ] **Step 8: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS (tous les modules) ; build sans warning. (Entre cette tâche et la suivante, les entiers partent en `x` même quand la méthode attend `u` : aucun test existant ne le vérifie ; la Task 3 rétablit et durcit la conversion.)

- [ ] **Step 9: Commit**

```bash
git add src/dbus/interfaceresolver.h src/dbus/interfaceresolver.cpp src/CMakeLists.txt src/dbus/dbusbridge.cpp tests/fixtures/echo_service.py tests/test_call_path.py tests/CMakeLists.txt
git commit -s -m "fix(dbus): one call path via createMethodCall + interface resolver (m1, m4)

Introspect the target object (no cache) to find the interface declaring
the method; call with an explicit interface instead of QDBusInterface,
which rejected the bus daemon and turned void replies into an invalid
QVariant.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: M6 — conversion stricte des scalaires, `as` et `ay`

**Files:**
- Create: `src/dbus/typecoercer.h`, `src/dbus/typecoercer.cpp`, `tests/test_coercion.py`
- Modify: `src/CMakeLists.txt`, `src/dbus/dbusbridge.cpp` (`callMethod`), `tests/fixtures/echo_service.py`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `MethodResolution`, `resolveMethod` (Task 2) ; `DBusBridge::jsonToVariant` (existant, public statique).
- Produces (étendu par la PR3b) : `namespace TypeCoercer { bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error); }`

- [ ] **Step 1: Fixture** — dans la classe `Echo` de `tests/fixtures/echo_service.py`, à côté des autres `Echo*` :

```python
    @_echo('t')
    def EchoT(self): pass
    @_echo('s')
    def EchoS(self): pass
```

- [ ] **Step 2: Tests** — créer `tests/test_coercion.py` :

```python
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
```

Ajouter `test_coercion` à la liste `_tests` de `tests/CMakeLists.txt`.

- [ ] **Step 3: Vérifier l'échec**

Run: `cmake --build build && ctest --test-dir build --output-on-failure -R test_coercion`
Expected: FAIL — `test_typed_scalars` (`x|[dbus.Int64(5)]` pour `EchoU`), `test_rejections` (la valeur part sans erreur), `test_byte_array_*`, `test_big_*`, `test_uint64_beyond_int64_needs_a_string`, `test_typed_without_interface_when_unique`, `test_integral_real_accepted`, `test_string_array_unchanged` (`av|…` depuis la Task 2). `test_variant_map_unchanged` et `test_other_containers_still_loose` passent.

- [ ] **Step 4: Créer `src/dbus/typecoercer.h`**

```cpp
// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace TypeCoercer {

// Convert one JSON argument into the QVariant to send for the D-Bus type
// `signature` (one complete type). Basic types, 'as' and 'ay' are converted
// strictly: a wrong JSON type, an out-of-range or non-integral number or bad
// base64 returns false with *error set, and nothing must be sent. Other types
// are converted loosely with DBusBridge::jsonToVariant (for now).
bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error);

} // namespace TypeCoercer
```

- [ ] **Step 5: Créer `src/dbus/typecoercer.cpp`**

```cpp
// SPDX-License-Identifier: MIT
#include "dbus/typecoercer.h"

#include "dbus/dbusbridge.h"

#include <QByteArray>
#include <QDBusObjectPath>
#include <QDBusSignature>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>
#include <limits>

namespace {

constexpr double kInt64Limit = 9223372036854775808.0; // 2^63

QString jsonText(const QJsonValue &value)
{
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

// Sign + magnitude, so the whole int64 and uint64 ranges fit.
struct ParsedInteger {
    bool negative = false;
    quint64 magnitude = 0;
};

// A JSON number that is integral and fits in int64 (Qt's parser keeps such
// integers exact), or a decimal string of any size (needed for large uint64).
bool parseInteger(const QJsonValue &value, ParsedInteger *out, QString *error)
{
    if (value.isString()) {
        QString text = value.toString().trimmed();
        out->negative = text.startsWith(QLatin1Char('-'));
        if (out->negative)
            text.remove(0, 1);
        bool ok = false;
        if (!text.isEmpty() && text.at(0).isDigit())
            out->magnitude = text.toULongLong(&ok, 10);
        if (!ok) {
            *error = QStringLiteral("%1 is not an integer").arg(jsonText(value));
            return false;
        }
        return true;
    }
    if (!value.isDouble()) {
        *error = QStringLiteral("expected an integer, got %1").arg(jsonText(value));
        return false;
    }
    const double d = value.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d) {
        *error = QStringLiteral("%1 is not an integer").arg(jsonText(value));
        return false;
    }
    if (std::fabs(d) >= kInt64Limit) {
        *error = QStringLiteral("%1 is beyond the int64 range, where JSON numbers lose digits: "
                                "pass it as a decimal string").arg(jsonText(value));
        return false;
    }
    // toInteger(), not the double: an integer such as 9007199254740993 is
    // stored exactly by the JSON parser but not representable as a double.
    const qint64 exact = value.toInteger();
    out->negative = exact < 0;
    out->magnitude = exact < 0 ? static_cast<quint64>(-(exact + 1)) + 1
                               : static_cast<quint64>(exact);
    return true;
}

struct IntegerRange {
    qint64 min;
    quint64 max;
};

bool integerRange(QChar type, IntegerRange *range)
{
    switch (type.toLatin1()) {
    case 'y': *range = {0, 255}; return true;
    case 'n': *range = {-32768, 32767}; return true;
    case 'q': *range = {0, 65535}; return true;
    case 'i': *range = {std::numeric_limits<qint32>::min(), std::numeric_limits<qint32>::max()}; return true;
    case 'u': *range = {0, std::numeric_limits<quint32>::max()}; return true;
    case 'x': *range = {std::numeric_limits<qint64>::min(),
                        static_cast<quint64>(std::numeric_limits<qint64>::max())}; return true;
    case 't': *range = {0, std::numeric_limits<quint64>::max()}; return true;
    default: return false;
    }
}

bool inRange(const ParsedInteger &n, const IntegerRange &range)
{
    if (!n.negative || n.magnitude == 0)
        return n.magnitude <= range.max;
    if (range.min >= 0)
        return false;
    // -(min + 1) + 1 avoids overflowing on INT64_MIN.
    return n.magnitude <= static_cast<quint64>(-(range.min + 1)) + 1;
}

qint64 signedValue(const ParsedInteger &n)
{
    if (!n.negative || n.magnitude == 0)
        return static_cast<qint64>(n.magnitude);
    return -static_cast<qint64>(n.magnitude - 1) - 1;
}

QVariant integerVariant(QChar type, const ParsedInteger &n)
{
    switch (type.toLatin1()) {
    case 'y': return QVariant::fromValue(static_cast<uchar>(n.magnitude));
    case 'n': return QVariant::fromValue(static_cast<short>(signedValue(n)));
    case 'q': return QVariant::fromValue(static_cast<ushort>(n.magnitude));
    case 'i': return QVariant::fromValue(static_cast<int>(signedValue(n)));
    case 'u': return QVariant::fromValue(static_cast<uint>(n.magnitude));
    case 'x': return QVariant::fromValue(static_cast<qlonglong>(signedValue(n)));
    default:  return QVariant::fromValue(static_cast<qulonglong>(n.magnitude)); // 't'
    }
}

bool coerceInteger(const QJsonValue &value, QChar type, QVariant *out, QString *error)
{
    IntegerRange range{};
    integerRange(type, &range);
    ParsedInteger n;
    if (!parseInteger(value, &n, error))
        return false;
    if (!inRange(n, range)) {
        *error = QStringLiteral("%1 out of range [%2,%3]")
                     .arg(jsonText(value)).arg(range.min).arg(range.max);
        return false;
    }
    *out = integerVariant(type, n);
    return true;
}

bool isObjectPath(const QString &path)
{
    static const QRegularExpression pattern(QStringLiteral("^/([A-Za-z0-9_]+(/[A-Za-z0-9_]+)*)?$"));
    return pattern.match(path).hasMatch();
}

bool coerceByteArray(const QJsonValue &value, QVariant *out, QString *error)
{
    if (value.isString()) {
        const auto decoded = QByteArray::fromBase64Encoding(
            value.toString().toLatin1(),
            QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
            *error = QStringLiteral("%1 is not valid base64").arg(jsonText(value));
            return false;
        }
        *out = decoded.decoded;
        return true;
    }
    if (!value.isArray()) {
        *error = QStringLiteral("expected an array of bytes or a base64 string, got %1")
                     .arg(jsonText(value));
        return false;
    }
    QByteArray bytes;
    const QJsonArray array = value.toArray();
    for (int i = 0; i < array.size(); ++i) {
        QVariant byte;
        QString byteError;
        if (!coerceInteger(array.at(i), QLatin1Char('y'), &byte, &byteError)) {
            *error = QStringLiteral("[%1]: %2").arg(i).arg(byteError);
            return false;
        }
        bytes.append(static_cast<char>(byte.value<uchar>()));
    }
    *out = bytes;
    return true;
}

} // namespace

namespace TypeCoercer {

bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error)
{
    if (signature.size() == 1) {
        const QChar type = signature.at(0);
        IntegerRange unused{};
        if (integerRange(type, &unused))
            return coerceInteger(value, type, out, error);
        switch (type.toLatin1()) {
        case 'b':
            if (!value.isBool()) {
                *error = QStringLiteral("expected a boolean, got %1").arg(jsonText(value));
                return false;
            }
            *out = value.toBool();
            return true;
        case 'd':
            if (!value.isDouble()) {
                *error = QStringLiteral("expected a number, got %1").arg(jsonText(value));
                return false;
            }
            *out = value.toDouble();
            return true;
        case 's':
        case 'o':
        case 'g':
            if (!value.isString()) {
                *error = QStringLiteral("expected a string, got %1").arg(jsonText(value));
                return false;
            }
            if (type == QLatin1Char('s')) {
                *out = value.toString();
            } else if (type == QLatin1Char('o')) {
                if (!isObjectPath(value.toString())) {
                    *error = QStringLiteral("%1 is not a valid object path").arg(jsonText(value));
                    return false;
                }
                *out = QVariant::fromValue(QDBusObjectPath(value.toString()));
            } else {
                *out = QVariant::fromValue(QDBusSignature(value.toString()));
            }
            return true;
        default:
            break;
        }
    } else if (signature == QLatin1String("ay")) {
        return coerceByteArray(value, out, error);
    } else if (signature == QLatin1String("as")) {
        const QJsonArray array = value.toArray();
        QStringList strings;
        bool ok = value.isArray();
        for (const QJsonValue &item : array) {
            ok = ok && item.isString();
            strings.append(item.toString());
        }
        if (!ok) {
            *error = QStringLiteral("expected an array of strings, got %1").arg(jsonText(value));
            return false;
        }
        *out = strings;
        return true;
    }

    *out = DBusBridge::jsonToVariant(value);
    return true;
}

} // namespace TypeCoercer
```

- [ ] **Step 6: CMake** — ajouter à `add_library(plasma-mcp-bridge-core SHARED …)`, après `dbus/interfaceresolver.cpp` :

```cmake
    dbus/typecoercer.cpp
```

- [ ] **Step 7: Brancher la coercition dans `callMethod`** — dans `src/dbus/dbusbridge.cpp`, ajouter `#include "dbus/typecoercer.h"` et remplacer :

```cpp
    QVariantList variantArgs;
    variantArgs.reserve(args.size());
    for (const QJsonValue &arg : args)
        variantArgs.append(jsonToVariant(arg));
```

par :

```cpp
    // Typed conversion when the introspection data names exactly one method
    // with this argument count; otherwise the loose mapping (the remote then
    // rejects a mismatch itself).
    const bool typed = resolution.state == MethodResolution::Unique
        && resolution.inSignature.size() == args.size();
    QVariantList variantArgs;
    variantArgs.reserve(args.size());
    for (int i = 0; i < args.size(); ++i) {
        if (!typed) {
            variantArgs.append(jsonToVariant(args.at(i)));
            continue;
        }
        QVariant value;
        QString error;
        if (!TypeCoercer::coerce(args.at(i), resolution.inSignature.at(i), &value, &error))
            return DBusResult::failure(QStringLiteral("argument %1 (%2): %3")
                                           .arg(i).arg(resolution.inSignature.at(i), error));
        variantArgs.append(value);
    }
```

- [ ] **Step 8: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS (tous les modules, dont 12 tests `test_coercion`) ; build sans warning.

- [ ] **Step 9: Commit**

```bash
git add src/dbus/typecoercer.h src/dbus/typecoercer.cpp src/CMakeLists.txt src/dbus/dbusbridge.cpp tests/fixtures/echo_service.py tests/test_coercion.py tests/CMakeLists.txt
git commit -s -m "fix(dbus): strict conversion of scalar, as and ay arguments (M6)

Arguments are converted to the types the introspection data declares
(also without 'interface' when the method name is unique): integers are
range-checked, non-integral or out-of-range values and wrong JSON types
are rejected before sending, ay accepts bytes or strict base64, and
integers beyond the int64 range can be passed as decimal strings.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Documentation, CHANGELOG, contrôle Qt 6.4.2

**Files:**
- Modify: `README.md` (section `## Usage`), `CLAUDE.md` (paragraphe `src/dbus/`), `CHANGELOG.md`

**Interfaces:**
- Consumes: comportement des Tasks 1-3.
- Produces: rien de nouveau.

- [ ] **Step 1: README (d5, d9)** — dans `## Usage`, remplacer :

```markdown
Any reply is marshalled back to JSON, including arrays, structs, and `a{sv}` maps.
Byte arrays (`ay`) come back as base64-encoded strings.
```

par :

```markdown
A method without return value — like `nextDesktop` above — replies `null`.
Replies are marshalled back to JSON, including arrays, structs and maps:
numbers are exact (a `uint64` above 2^53 comes back as a decimal string), byte
arrays (`ay`) come back as base64 strings, and a value of a type that cannot be
represented is replaced by `"<unsupported D-Bus type '<signature>'>"`.

Arguments are converted to the types the method declares in its introspection
data — also when `interface` is omitted, as long as the method name is unique
on the object. Integers are range-checked (pass a `uint64` beyond the int64 range
as a decimal string), `ay` accepts an array of bytes or a base64 string, and a value that
does not fit is rejected before anything is sent. Container arguments other
than `as`, `ay` and `a{sv}` are not converted yet.
```

- [ ] **Step 2: CLAUDE.md** — dans le paragraphe `src/dbus/`, remplacer :

```markdown
  replies (arrays, structs, `a{sv}` maps) into JSON. Argument type coercion (e.g. a
  JSON int into a uint32) happens by routing calls through `QDBusInterface` when an
  interface name is supplied.
```

par :

```markdown
  replies (arrays, structs, `a{sv}` maps) into JSON. `callMethod` introspects the
  target object on every call (`dbus/interfaceresolver.*`, no cache) to find the
  interface declaring the method and its input signature, converts each argument
  to that signature (`dbus/typecoercer.*`; strict for basic types, `as`, `ay`), and
  sends a plain `QDBusMessage::createMethodCall` with the explicit interface —
  never `QDBusInterface`, which rejects the bus daemon and mangles void replies.
  Both units are internal: their headers are not installed.
```

- [ ] **Step 3: CHANGELOG** — dans `## 0.2.0 (unreleased)`, ajouter à `### Fixed` :

```markdown
- Numbers in replies are exact (`123456789`, `3.14159265`), not rounded to six
  significant digits.
- A method without return value replies `null` (was `<unrepresentable:>` when
  `interface` was given).
- `interface: "org.freedesktop.DBus"` works for calls to the bus daemon.
- Arguments `u`, `y`, `n`, `o`, … are sent with the declared type even when
  `interface` is omitted (method name unique on the object).
```

et à `### Changed` :

```markdown
- Arguments of basic types, `as` and `ay` are converted strictly: out-of-range
  or non-integral integers (e.g. `-1` for `u`, `300` for `y`, `2.7` for `n`) and
  wrong JSON types now fail with `argument <n> (<type>): …` instead of being
  wrapped, rounded or sent as another type. Integers beyond the int64 range
  must be passed as decimal strings; `ay` accepts base64.
- A `uint64` above 2^53 is returned as a decimal string.
```

- [ ] **Step 4: Vérifier en local et en Qt 6.4.2**

Run (local) : `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: tous les modules PASS.

Run (Qt 6.4.2, image `ubuntu:24.04` avec les paquets de la CI ; un script dans un fichier si l'environnement refuse les `bash -c` en ligne) :
```bash
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src plasma-mcp-ci:noble bash -c '
  cmake -S /src -B /tmp/b -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DPython3_EXECUTABLE=/usr/bin/python3 >/tmp/c.log &&
  cmake --build /tmp/b >/tmp/b.log 2>&1 && grep -c "warning:" /tmp/b.log; ctest --test-dir /tmp/b --output-on-failure --no-tests=error'
```
Expected: `0` warning, tous les modules PASS. (L'image `plasma-mcp-ci:noble` a été construite pendant la PR2 ; sinon la construire à partir de `ubuntu:24.04` + `build-essential cmake ninja-build extra-cmake-modules qt6-base-dev qt6-base-dev-tools dbus-daemon python3-dbus python3-gi git`.)

- [ ] **Step 5: Commit**

```bash
git add README.md CLAUDE.md CHANGELOG.md
git commit -s -m "docs: exact numbers, null replies, argument conversion rules

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Fin de la PR3a

- [ ] `git log --oneline remediation/pr1-harness-b1..` montre 4 commits, tous signés (`git log --format=%B | grep -c Signed-off-by` = 4) ; `git status` propre.
- [ ] Ne pas pousser ni ouvrir la PR sans l'accord de l'utilisateur. La PR (base `main` après merge de la PR1, sinon base `remediation/pr1-harness-b1`) liste M1, m1, m4, M6, d5, d9 et se termine par `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
