# PR3b — Coercition des conteneurs, variants typés, `Properties.Set`, marqueur `h` — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convertir les arguments conteneurs (`au`, `a{ss}`, `(si)`, `a(si)`, `aas`, `a{sa{sv}}`…) et les variants d'après la signature déclarée, au lieu de les envoyer en `av`/`a{sv}` (M2, M6) ; typer le `v` de `Properties.Set` d'après la propriété déclarée ; remplacer `<unrepresentable:QDBusUnixFileDescriptor>` par un marqueur explicite (m2) ; appliquer le mapping naturel `i`/`x` (m12) ; renvoyer un `x` hors ±2^53 en chaîne.

**Architecture:** `src/dbus/typecoercer.cpp` est réécrit en un écrivain récursif **piloté par la signature parsée**, en deux passes : `checkValue` (validation complète, aucune construction) puis `build`/`write` (construction qui ne peut plus échouer, donc aucun `begin*()` sans son `end*()` — libdbus appelle `abort()` sur un contenu de tableau qui ne correspond pas au type d'élément). Les types d'élément de tableau et de valeur de map passent par une table de `QMetaType` codée en dur (natifs + ensemble curé enregistré par `qDBusRegisterMetaType`). `DBusBridge::jsonToVariant` applique le mapping naturel ; `callMethod` type le `v` de `Properties.Set` grâce à `propertyTypeFromXml`.

**Tech Stack:** C++17 (`std::apply`, fold expressions), Qt 6 Core + DBus (≥ 6.4), CMake, tests Python du harnais sur bus privé.

**Spec:** `docs/superpowers/specs/2026-10-01-remediation-core-design.md` — §4.3 (A2), §2 (M2, M6, m2, m12, d5, d9), §9 (changelog), §10 (PR3b).

## Global Constraints

- **Base :** branche `remediation/pr3b-containers` depuis `main` @ `20002cc` (PR1, 3a, 5, 6 mergées). Ce plan en est le premier commit.
- **DCO :** `git commit -s` (identité `fredaime <frederic.aime@gmail.com>`), messages terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Seule dépendance : Qt 6 Core + DBus. `-Wall -Wextra -Werror` sur Qt 6.4.2 (CI) et Qt 6.11 (local).
- Aucun en-tête installé ne change de structure ; `typecoercer.h` et `interfaceresolver.h` restent internes. Pas d'usage de `QDBusMetaType::typeToSignature` (API `\internal`) dans le code de production.
- `Tool::call()` tourne sur des workers (PR6) : toute donnée statique du coercer est initialisée une fois de façon thread-safe (statique locale) et n'est ensuite que lue.
- Format des erreurs : `argument <n> (<signature>): <chemin>: <motif>`, chemins `[<index>]`, `['<clé>']`, `key '<clé>'`, `field <index>`. Motifs exacts nouveaux :
  - `expected an array for '<sig>', got <valeur>`
  - `expected an object for '<sig>', got <valeur>`
  - `expected an array of <N> fields for '<sig>', got <valeur>`
  - `unsupported D-Bus signature '<elem>' (array element)` / `(map value)`
  - `unix fd arguments are not supported (they cannot travel over MCP)`
  - `<valeur> is not a valid D-Bus signature`
  - `<valeur> is not a single complete D-Bus type` (pour `@dbus`) ; `'<sig>' is not a single complete D-Bus type` (signature déclarée)
  - `null cannot be sent in a D-Bus variant`
  - `nested deeper than 64 levels`
  Les motifs de la PR3a (basiques, `as`, `ay`) restent identiques.
- Sortie : `"<unix fd: not transferable over MCP>"` pour un `h` ; un `x` hors [-2^53, 2^53] en chaîne décimale.
- **Écarts à la spec, consignés dans la spec (Task 5)** :
  1. **Structs curées sans `std::tuple`** : la spec s'appuie sur le `operator<<` de `std::tuple` de `qdbusargument.h`, qui n'existe pas en Qt 6.4.2 (vérifié dans l'image CI : aucun `tuple` dans l'en-tête). Le plan définit un `DBusStruct<T...>` interne avec ses propres opérateurs (même rendu, prouvé sur 6.4.2 par le spike avec des structs explicites).
  2. **Question ouverte « `x` au-delà de 2^53 » tranchée** : un `x` hors ±2^53 sort en chaîne décimale, comme un `t` (un client JavaScript lit les nombres JSON en double et perdrait des chiffres).
- Tests : bus privé uniquement ; assertions sur les noms d'erreur D-Bus, nos messages de coercition étant assertés tels quels.
- Conséquence hors dépôt : aucune description d'outil ne change (`--emit-skill` identique) ; la prose de la skill enterprise (PR7) pourra documenter `@dbus`.

## Review Focus

- **Variant naturel imbriqué** (`[1, [2, "x"], {"k": null}]` dans un `v`) : un `null` profond donne une erreur avec son chemin, rien n'est envoyé → cas `test_variant_rejections` (Task 3).
- **Clé de map non convertible** (`{"x": 1}` pour `a{iu}`) : erreur `key 'x': …` avant envoi → cas de `test_container_rejections` (Task 3).
- **Erreur au milieu d'un tableau de structs** (`[["a",1],["b","x"]]` pour `a(si)`) : erreur de chemin `[1]: field 1: "x" is not an integer`, le bridge ne meurt pas (aucun `beginArray` ouvert) → cas de `test_container_rejections` (Task 3).
- **Signature déclarée hors table** (élément `(sssuda{sv})`) : erreur `unsupported` même pour un tableau vide → cas `test_unsupported_element` (Task 3).
- **Valeur de propriété hors plage** (`300` pour une propriété `y`) : erreur de plage, pas d'envoi en `i` → `test_declared_type_is_strict` (Task 4).

---

## File Structure

| Fichier | Responsabilité |
|---|---|
| `src/dbus/dbusbridge.cpp` (modifié) | `variantToJson` : marqueur `h`, `x` hors ±2^53 en chaîne ; `jsonToVariant` : mapping naturel (m12) ; `callMethod` : type du `v` de `Properties.Set`. |
| `src/dbus/typecoercer.h/.cpp` (réécrits) | Signatures, table des types d'élément, `checkValue`, `build`/`write`, `coerce`. |
| `src/dbus/interfaceresolver.h/.cpp` (modifiés) | `MethodResolution::introspection`, `propertyTypeFromXml`. |
| `tests/fixtures/echo_service.py` (modifié) | `_echo` à N arguments, nouveaux `Echo*`, `RetAXBig`, objets `/Loose` et `/Props`. |
| `tests/test_demarshall.py`, `tests/test_coercion.py` (modifiés) | Sortie (`h`, `x`), conteneurs, variants, `Properties.Set`, fuzz. |
| `README.md`, `CLAUDE.md`, `CHANGELOG.md`, spec (modifiés) | Règles de conversion, `@dbus`, écarts. |

Commandes (depuis la racine du worktree) :
- construire : `cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" && cmake --build build`
- une classe : `cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_coercion.Containers; cd ..`
- tout : `ctest --test-dir build --output-on-failure --no-tests=error`

---

### Task 1: Sortie — marqueur `h` (m2) et `x` hors ±2^53 en chaîne

**Files:**
- Modify: `src/dbus/dbusbridge.cpp` (`variantToJson`), `tests/fixtures/echo_service.py`, `tests/test_demarshall.py`

**Interfaces:**
- Consumes: `Demarshall.scalar(method)`, `Demarshall.ret(method)` (`tests/test_demarshall.py`), méthode `RetH` de la fixture.
- Produces: méthode de fixture `RetAXBig() -> ax` (`[2^53, 2^53 + 1, -(2^53 + 1)]`).

- [ ] **Step 1: Écrire les tests qui échouent**

Dans `tests/fixtures/echo_service.py`, après `RetX` :

```python
    @dbus.service.method(IFACE, in_signature='', out_signature='ax')
    def RetAXBig(self): return [2 ** 53, 2 ** 53 + 1, -(2 ** 53 + 1)]
```

Dans `tests/test_demarshall.py`, après `test_uint64_beyond_2_53_is_a_decimal_string` :

```python
    def test_int64_beyond_2_53_is_a_decimal_string(self):
        self.assertEqual(self.ret('RetAXBig'),
                         [9007199254740992, '9007199254740993', '-9007199254740993'])

    def test_unix_fd_is_a_marker(self):
        self.assertEqual(self.scalar('RetH'), ('<unix fd: not transferable over MCP>', False))
```

- [ ] **Step 2: Vérifier l'échec**

Run: `cmake -S . -B build -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" >/dev/null && cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_demarshall; cd ..`
Expected: FAIL `test_int64_beyond_2_53_is_a_decimal_string` (nombres, pas chaînes) et `test_unix_fd_is_a_marker` (`<unrepresentable:QDBusUnixFileDescriptor>`) ; les autres PASS.

- [ ] **Step 3: Implémenter**

Dans `src/dbus/dbusbridge.cpp`, `variantToJson` : ajouter, juste avant `switch (value.typeId()) {` :

```cpp
    if (value.metaType() == QMetaType::fromType<QDBusUnixFileDescriptor>())
        return QStringLiteral("<unix fd: not transferable over MCP>");
```

et remplacer

```cpp
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QMetaType::UInt:
    case QMetaType::LongLong:
        return static_cast<qint64>(value.toLongLong());
```

par

```cpp
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QMetaType::UInt:
        return static_cast<qint64>(value.toLongLong());
    case QMetaType::LongLong: {
        // Beyond +-2^53 a JSON number no longer holds the value exactly
        // (clients parse numbers as doubles): emit the exact decimal string.
        const qlonglong x = value.toLongLong();
        const qlonglong limit = Q_INT64_C(1) << 53;
        if (x >= -limit && x <= limit)
            return static_cast<qint64>(x);
        return QString::number(x);
    }
```

- [ ] **Step 4: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS.

- [ ] **Step 5: Commit**

```bash
git add src/dbus/dbusbridge.cpp tests/fixtures/echo_service.py tests/test_demarshall.py
git commit -s -m "Mark unix fds in replies; int64 beyond 2^53 as a decimal string (m2)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Mapping naturel des entiers (m12) sur le chemin non typé

**Files:**
- Modify: `src/dbus/dbusbridge.cpp` (`jsonToVariant`), `tests/fixtures/echo_service.py`, `tests/test_coercion.py`

**Interfaces:**
- Consumes: `FixtureTestCase`, `ECHO`.
- Produces: objet de fixture `/Loose` (introspection vide, méthode `EchoAny` de signature libre sur l'interface `org.plasmamcp.Validation`) ; `DBusBridge::jsonToVariant` : entier → `int` s'il tient sur 32 bits, sinon `qlonglong` ; non entier → `double`. Consommé par Task 3 (contenu naturel d'un `v`).

- [ ] **Step 1: Écrire le test qui échoue**

Dans `tests/fixtures/echo_service.py`, après la classe `Nested` :

```python
class Loose(dbus.service.Object):
    """No introspection data: the bridge cannot type the arguments, so they
    take the natural mapping. EchoAny accepts any signature."""

    @dbus.service.method('org.freedesktop.DBus.Introspectable', in_signature='',
                         out_signature='s')
    def Introspect(self):
        return '<node/>'

    @dbus.service.method(IFACE, out_signature='s', message_keyword='msg')
    def EchoAny(self, *args, msg=None):
        return '%s|%r' % (msg.get_signature(), list(args))
```

et dans `main()`, après `Nested(bus, '/Nested')` : `Loose(bus, '/Loose')`. Compléter la docstring du module : `- /Loose answers EchoAny(any signature) but has no introspection data.`

Dans `tests/test_coercion.py`, avant `if __name__ == '__main__':` :

```python
class NaturalMapping(FixtureTestCase):
    """Without introspection data the JSON types decide (m12)."""

    def test_natural_types(self):
        reply = self.bridge().call('dbus_call', {
            'service': ECHO['service'], 'path': '/Loose', 'interface': ECHO['interface'],
            'method': 'EchoAny',
            'args': [7, -2147483648, 2147483648, 5000000000, 2.5, 'x', True, [1, 2], {'k': 1}]})
        self.assertFalse(reply.is_error, reply.text)
        self.assertEqual(reply.text.split('|')[0], 'iixxdsbava{sv}')
```

- [ ] **Step 2: Vérifier l'échec**

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_coercion.NaturalMapping; cd ..`
Expected: FAIL, signature reçue `xxxxdsbava{sv}` (tout entier part en `x` aujourd'hui).

- [ ] **Step 3: Implémenter**

Dans `src/dbus/dbusbridge.cpp`, `jsonToVariant`, remplacer la branche `QJsonValue::Double` par :

```cpp
    case QJsonValue::Double: {
        // The natural mapping (m12): an integer is an 'i' when it fits in 32
        // bits, an 'x' otherwise; anything else is a 'd'. Qt keeps integers
        // that fit in int64 exact; two defaults tell such a value apart.
        const qint64 exactA = value.toInteger(0);
        const qint64 exactB = value.toInteger(1);
        if (exactA != exactB)
            return value.toDouble();
        if (exactA >= std::numeric_limits<int>::min() && exactA <= std::numeric_limits<int>::max())
            return QVariant(static_cast<int>(exactA));
        return QVariant(static_cast<qlonglong>(exactA));
    }
```

- [ ] **Step 4: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: tous les modules PASS (`test_call_path.test_ambiguous_method_keeps_interface_empty` envoie `5` en `i` et reçoit toujours une réponse `None|…` : il n'assertait pas la signature).

- [ ] **Step 5: Commit**

```bash
git add src/dbus/dbusbridge.cpp tests/fixtures/echo_service.py tests/test_coercion.py
git commit -s -m "Natural mapping: an integer that fits in 32 bits is an 'i' (m12)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Coercer piloté par la signature — conteneurs, variants, `@dbus` (M2, M6)

**Files:**
- Modify: `src/dbus/typecoercer.h`, `src/dbus/typecoercer.cpp` (réécrit en entier), `tests/fixtures/echo_service.py`, `tests/test_coercion.py`

**Interfaces:**
- Consumes: `DBusBridge::jsonToVariant` naturel (Task 2) ; helpers PR3a de `typecoercer.cpp` (`jsonText`, `parseInteger`, `coerceInteger`, `isObjectPath`, `coerceByteArray`…), conservés à l'identique.
- Produces: `bool TypeCoercer::coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error)` (signature inchangée, comportement étendu) ; fixture : `_echo` à N arguments et méthodes `EchoI EchoAI EchoAX EchoAT EchoAD EchoAB EchoG EchoH EchoASI EchoAStructAI EchoAAS EchoAAY EchoAAI EchoAASV EchoASASV EchoASASS EchoASAIU EchoNested EchoMapIU EchoUnsupported EchoMapUnsupported`.

- [ ] **Step 1: Étendre la fixture**

Dans `tests/fixtures/echo_service.py`, remplacer `_echo` par :

```python
def _echo(signature):
    """Echo<X>: replies "<wire signature>|<repr of the received args>"."""
    names = ['a%d' % i for i in range(len(list(dbus.Signature(signature))))]

    def decorator(fn):
        namespace = {}
        # dbus-python needs one named parameter per argument of the signature.
        exec('def method(self, %s, msg=None):\n'
             '    return "%%s|%%r" %% (msg.get_signature(), [%s])\n'
             % (', '.join(names), ', '.join(names)), namespace)
        method = namespace['method']
        method.__name__ = fn.__name__
        return dbus.service.method(IFACE, in_signature=signature, out_signature='s',
                                   message_keyword='msg')(method)
    return decorator
```

et ajouter dans `Echo`, après `EchoS` :

```python
    @_echo('i')
    def EchoI(self): pass
    @_echo('ai')
    def EchoAI(self): pass
    @_echo('ax')
    def EchoAX(self): pass
    @_echo('at')
    def EchoAT(self): pass
    @_echo('ad')
    def EchoAD(self): pass
    @_echo('ab')
    def EchoAB(self): pass
    @_echo('g')
    def EchoG(self): pass
    @_echo('h')
    def EchoH(self): pass
    @_echo('a(si)')
    def EchoASI(self): pass
    @_echo('a(ai)')
    def EchoAStructAI(self): pass
    @_echo('aas')
    def EchoAAS(self): pass
    @_echo('aay')
    def EchoAAY(self): pass
    @_echo('aai')
    def EchoAAI(self): pass
    @_echo('aa{sv}')
    def EchoAASV(self): pass
    @_echo('a{sa{sv}}')
    def EchoASASV(self): pass
    @_echo('a{sa{ss}}')
    def EchoASASS(self): pass
    @_echo('asaiu')
    def EchoASAIU(self): pass
    @_echo('(a(si)v)')
    def EchoNested(self): pass
    @_echo('a{iu}')
    def EchoMapIU(self): pass
    @_echo('a(sssuda{sv})')
    def EchoUnsupported(self): pass
    @_echo('a{s(sssuda{sv})}')
    def EchoMapUnsupported(self): pass
```

- [ ] **Step 2: Écrire les tests qui échouent**

Dans `tests/test_coercion.py` : supprimer `test_other_containers_still_loose`, puis ajouter avant `class NaturalMapping` :

```python
SI_A1 = "dbus.Struct((dbus.String('a'), dbus.Int32(1)), signature=None)"
SI_B2 = "dbus.Struct((dbus.String('b'), dbus.Int32(2)), signature=None)"


class Containers(FixtureTestCase):

    def send(self, method, *values):
        return self.bridge().call('dbus_call', dict(ECHO, method=method, args=list(values)))

    def assertSent(self, method, values, expected):
        self.assertEqual(self.send(method, *values), (expected, False))

    def test_arrays_of_basic_types(self):
        self.assertSent('EchoAU', [[1, 2, 3]],
                        "au|[dbus.Array([dbus.UInt32(1), dbus.UInt32(2), dbus.UInt32(3)], "
                        "signature=dbus.Signature('u'))]")
        self.assertSent('EchoAU', [[]], "au|[dbus.Array([], signature=dbus.Signature('u'))]")
        self.assertSent('EchoAI', [[1, -2, 3]],
                        "ai|[dbus.Array([dbus.Int32(1), dbus.Int32(-2), dbus.Int32(3)], "
                        "signature=dbus.Signature('i'))]")
        self.assertSent('EchoAX', [[-1, 4000000000]],
                        "ax|[dbus.Array([dbus.Int64(-1), dbus.Int64(4000000000)], "
                        "signature=dbus.Signature('x'))]")
        self.assertSent('EchoAT', [[1, '18446744073709551615']],
                        "at|[dbus.Array([dbus.UInt64(1), dbus.UInt64(18446744073709551615)], "
                        "signature=dbus.Signature('t'))]")
        self.assertSent('EchoAD', [[0.5, 2]],
                        "ad|[dbus.Array([dbus.Double(0.5), dbus.Double(2.0)], "
                        "signature=dbus.Signature('d'))]")
        self.assertSent('EchoAB', [[True, False]],
                        "ab|[dbus.Array([dbus.Boolean(True), dbus.Boolean(False)], "
                        "signature=dbus.Signature('b'))]")

    def test_maps(self):
        self.assertSent('EchoASS', [{'k': 'v', 'k2': 'w'}],
                        "a{ss}|[dbus.Dictionary({dbus.String('k'): dbus.String('v'), "
                        "dbus.String('k2'): dbus.String('w')}, signature=dbus.Signature('ss'))]")
        self.assertSent('EchoMapIU', [{'1': 2}],
                        "a{iu}|[dbus.Dictionary({dbus.Int32(1): dbus.UInt32(2)}, "
                        "signature=dbus.Signature('iu'))]")
        self.assertSent('EchoASASV', [{'eth0': {'mtu': 1500, 'up': True}}],
                        "a{sa{sv}}|[dbus.Dictionary({dbus.String('eth0'): dbus.Dictionary("
                        "{dbus.String('mtu'): dbus.Int32(1500, variant_level=1), "
                        "dbus.String('up'): dbus.Boolean(True, variant_level=1)}, "
                        "signature=dbus.Signature('sv'))}, signature=dbus.Signature('sa{sv}'))]")
        self.assertSent('EchoASASS', [{'g': {'k': 'v'}}],
                        "a{sa{ss}}|[dbus.Dictionary({dbus.String('g'): dbus.Dictionary("
                        "{dbus.String('k'): dbus.String('v')}, signature=dbus.Signature('ss'))}, "
                        "signature=dbus.Signature('sa{ss}'))]")

    def test_structs(self):
        self.assertSent('EchoStruct', [['a', 1]], '(si)|[%s]' % SI_A1)
        self.assertSent('EchoASI', [[['a', 1], ['b', 2]]],
                        "a(si)|[dbus.Array([%s, %s], signature=dbus.Signature('(si)'))]"
                        % (SI_A1, SI_B2))
        self.assertSent('EchoASI', [[]], "a(si)|[dbus.Array([], signature=dbus.Signature('(si)'))]")
        self.assertSent('EchoAStructAI', [[[[1, 2]], [[3]]]],
                        "a(ai)|[dbus.Array([dbus.Struct((dbus.Array([dbus.Int32(1), "
                        "dbus.Int32(2)], signature=dbus.Signature('i')),), signature=None), "
                        "dbus.Struct((dbus.Array([dbus.Int32(3)], signature=dbus.Signature('i')),"
                        "), signature=None)], signature=dbus.Signature('(ai)'))]")
        self.assertSent('EchoOX', ['/a/b', 123], "ox|[dbus.ObjectPath('/a/b'), dbus.Int64(123)]")

    def test_nested_arrays(self):
        self.assertSent('EchoAAS', [[['a', 'b'], ['c']]],
                        "aas|[dbus.Array([dbus.Array([dbus.String('a'), dbus.String('b')], "
                        "signature=dbus.Signature('s')), dbus.Array([dbus.String('c')], "
                        "signature=dbus.Signature('s'))], signature=dbus.Signature('as'))]")
        self.assertSent('EchoAAY', [[[104, 105], 'aGk=']],
                        "aay|[dbus.Array([dbus.Array([dbus.Byte(104), dbus.Byte(105)], "
                        "signature=dbus.Signature('y')), dbus.Array([dbus.Byte(104), "
                        "dbus.Byte(105)], signature=dbus.Signature('y'))], "
                        "signature=dbus.Signature('ay'))]")
        self.assertSent('EchoAAI', [[[1, 2], [3]]],
                        "aai|[dbus.Array([dbus.Array([dbus.Int32(1), dbus.Int32(2)], "
                        "signature=dbus.Signature('i')), dbus.Array([dbus.Int32(3)], "
                        "signature=dbus.Signature('i'))], signature=dbus.Signature('ai'))]")
        self.assertSent('EchoAASV', [[{'a': 1}, {'b': 'x'}]],
                        "aa{sv}|[dbus.Array([dbus.Dictionary({dbus.String('a'): "
                        "dbus.Int32(1, variant_level=1)}, signature=dbus.Signature('sv')), "
                        "dbus.Dictionary({dbus.String('b'): dbus.String('x', variant_level=1)}, "
                        "signature=dbus.Signature('sv'))], signature=dbus.Signature('a{sv}'))]")

    def test_several_arguments(self):
        self.assertSent('EchoASAIU', [['a'], [1, 2], 7],
                        "asaiu|[dbus.Array([dbus.String('a')], signature=dbus.Signature('s')), "
                        "dbus.Array([dbus.Int32(1), dbus.Int32(2)], signature=dbus.Signature('i')), "
                        "dbus.UInt32(7)]")

    def test_signature_argument(self):
        self.assertSent('EchoG', ['a{sv}'], "g|[dbus.Signature('a{sv}')]")

    def test_container_rejections(self):
        cases = [
            ('EchoAU', [1, -1], 'argument 0 (au): [1]: -1 out of range [0,4294967295]'),
            ('EchoAU', 5, "argument 0 (au): expected an array for 'au', got 5"),
            ('EchoStruct', ['a'],
             'argument 0 ((si)): expected an array of 2 fields for \'(si)\', got ["a"]'),
            ('EchoASI', [['a', 1], ['b', 'x']],
             'argument 0 (a(si)): [1]: field 1: "x" is not an integer'),
            ('EchoASS', {'k': 1}, "argument 0 (a{ss}): ['k']: expected a string, got 1"),
            ('EchoASS', [1], "argument 0 (a{ss}): expected an object for 'a{ss}', got [1]"),
            ('EchoMapIU', {'x': 1}, 'argument 0 (a{iu}): key \'x\': "x" is not an integer'),
            ('EchoH', 0,
             'argument 0 (h): unix fd arguments are not supported (they cannot travel over MCP)'),
            ('EchoG', 'a{', 'argument 0 (g): "a{" is not a valid D-Bus signature'),
        ]
        for method, value, message in cases:
            with self.subTest(method=method, value=value):
                self.assertEqual(self.send(method, value), (message, True))

    def test_unsupported_element(self):
        self.assertEqual(self.send('EchoUnsupported', []),
                         ("argument 0 (a(sssuda{sv})): unsupported D-Bus signature "
                          "'(sssuda{sv})' (array element)", True))
        self.assertEqual(self.send('EchoMapUnsupported', {}),
                         ("argument 0 (a{s(sssuda{sv})}): unsupported D-Bus signature "
                          "'(sssuda{sv})' (map value)", True))


class Variants(FixtureTestCase):

    def send(self, method, value):
        return self.bridge().call('dbus_call', dict(ECHO, method=method, args=[value]))

    def test_natural_variants(self):
        self.assertEqual(self.send('EchoV', 0.5), ('v|[dbus.Double(0.5, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', 7), ('v|[dbus.Int32(7, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', 5000000000),
                         ('v|[dbus.Int64(5000000000, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', {'k': 1, 'l': ['x']}),
                         ("v|[dbus.Dictionary({dbus.String('k'): dbus.Int32(1, variant_level=1), "
                          "dbus.String('l'): dbus.Array([dbus.String('x', variant_level=2)], "
                          "signature=dbus.Signature('v'), variant_level=1)}, "
                          "signature=dbus.Signature('sv'), variant_level=1)]", False))

    def test_explicit_variant_type(self):
        self.assertEqual(self.send('EchoV', {'@dbus': 'y', 'value': 2}),
                         ('v|[dbus.Byte(2, variant_level=1)]', False))
        self.assertEqual(self.send('EchoV', {'@dbus': 'a(si)', 'value': [['a', 1]]}),
                         ("v|[dbus.Array([%s], signature=dbus.Signature('(si)'), "
                          "variant_level=1)]" % SI_A1, False))
        self.assertEqual(self.send('EchoASV', {'n': 1, 's': 'x', 'urg': {'@dbus': 'y', 'value': 2}}),
                         ("a{sv}|[dbus.Dictionary({dbus.String('n'): dbus.Int32(1, variant_level=1), "
                          "dbus.String('s'): dbus.String('x', variant_level=1), "
                          "dbus.String('urg'): dbus.Byte(2, variant_level=1)}, "
                          "signature=dbus.Signature('sv'))]", False))

    def test_variant_inside_a_struct(self):
        reply = self.send('EchoNested', [[['a', 1]], {'@dbus': 'a(ai)', 'value': [[[1]]]}])
        self.assertEqual(reply, (
            "(a(si)v)|[dbus.Struct((dbus.Array([%s], signature=dbus.Signature('(si)')), "
            "dbus.Array([dbus.Struct((dbus.Array([dbus.Int32(1)], signature=dbus.Signature('i')),),"
            " signature=None)], signature=dbus.Signature('(ai)'), variant_level=1)), "
            "signature=None)]" % SI_A1, False))

    def test_dbus_key_has_no_meaning_elsewhere(self):
        reply = self.send('EchoASS', {'@dbus': 'y', 'value': 'x'})
        self.assertFalse(reply.is_error, reply.text)
        self.assertIn("dbus.String('@dbus'): dbus.String('y')", reply.text)

    def test_variant_rejections(self):
        cases = [
            (None, 'argument 0 (v): null cannot be sent in a D-Bus variant'),
            ([1, [2, 'x'], {'k': None}],
             "argument 0 (v): [2]: ['k']: null cannot be sent in a D-Bus variant"),
            ({'@dbus': 'ii', 'value': 1},
             'argument 0 (v): "ii" is not a single complete D-Bus type'),
            ({'@dbus': 'y', 'value': 300}, 'argument 0 (v): 300 out of range [0,255]'),
            ({'@dbus': 'a(sssuda{sv})', 'value': []},
             "argument 0 (v): unsupported D-Bus signature '(sssuda{sv})' (array element)"),
        ]
        for value, message in cases:
            with self.subTest(value=value):
                self.assertEqual(self.send('EchoV', value), (message, True))
        reply = self.send('EchoASV', {'urg': {'@dbus': 'y', 'value': 300}})
        self.assertEqual(reply, ("argument 0 (a{sv}): ['urg']: 300 out of range [0,255]", True))
```

- [ ] **Step 3: Vérifier l'échec**

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_coercion; cd ..`
Expected: FAIL ou ERROR pour les tests `Containers` et `Variants` (les conteneurs partent en `av`/`a{sv}`, un `v` part sans enveloppe de variant donc le service le rejette, aucune erreur de chemin, `h` et `g` invalides envoyés tels quels) ; seul `test_signature_argument` peut déjà passer ; les tests PR3a de `Coercion` et `NaturalMapping` restent PASS.

- [ ] **Step 4: Réécrire le coercer**

`src/dbus/typecoercer.h` — remplacer le commentaire de `coerce` :

```cpp
// Convert one JSON argument into the QVariant to send for the D-Bus type
// `signature` (one complete type), driven by the parsed signature: basic
// types, arrays, maps, structs and variants ({"@dbus": "<type>", "value": …}
// forces a variant's type, otherwise the natural mapping applies). The value
// is checked in full before anything is built; on error, returns false with
// *error set ("[1]: field 0: …"), and nothing must be sent.
```

`src/dbus/typecoercer.cpp` — garder intégralement l'en-tête de licence et, dans l'espace anonyme, les fonctions PR3a `kInt64Limit`, `jsonText`, `ParsedInteger`, `parseInteger`, `IntegerRange`, `integerRange`, `inRange`, `signedValue`, `integerVariant`, `coerceInteger`, `isObjectPath`, `coerceByteArray`. Remplacer les includes par :

```cpp
#include "dbus/typecoercer.h"

#include "dbus/dbusbridge.h"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusSignature>
#include <QDBusVariant>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>
#include <limits>
#include <tuple>
```

puis, toujours dans l'espace anonyme, **après** `coerceByteArray`, ajouter :

```cpp
// ------------------------------------------------------------ signatures

constexpr int kMaxDepth = 64; // D-Bus allows 32 array plus 32 struct levels

bool isBasicType(QChar type)
{
    return QStringLiteral("ybnqiuxtdsogh").contains(type);
}

// End of the complete type that starts at `pos`, or -1 when `sig` is
// malformed there.
qsizetype completeTypeEnd(const QString &sig, qsizetype pos, int depth)
{
    if (pos >= sig.size() || depth > kMaxDepth)
        return -1;
    const QChar type = sig.at(pos);
    if (isBasicType(type) || type == QLatin1Char('v'))
        return pos + 1;
    if (type == QLatin1Char('a')) {
        if (pos + 1 < sig.size() && sig.at(pos + 1) == QLatin1Char('{')) {
            const qsizetype key = pos + 2;
            if (key >= sig.size() || !isBasicType(sig.at(key)))
                return -1;
            const qsizetype end = completeTypeEnd(sig, key + 1, depth + 1);
            if (end < 0 || end >= sig.size() || sig.at(end) != QLatin1Char('}'))
                return -1;
            return end + 1;
        }
        return completeTypeEnd(sig, pos + 1, depth + 1);
    }
    if (type == QLatin1Char('(')) {
        qsizetype p = pos + 1;
        int fields = 0;
        while (p < sig.size() && sig.at(p) != QLatin1Char(')')) {
            p = completeTypeEnd(sig, p, depth + 1);
            if (p < 0)
                return -1;
            ++fields;
        }
        if (p >= sig.size() || fields == 0)
            return -1;
        return p + 1;
    }
    return -1;
}

// The complete types of `sig`, in order; empty when `sig` is malformed.
QStringList splitTypes(const QString &sig)
{
    QStringList types;
    qsizetype p = 0;
    while (p < sig.size()) {
        const qsizetype end = completeTypeEnd(sig, p, 0);
        if (end < 0)
            return {};
        types.append(sig.mid(p, end - p));
        p = end;
    }
    return types;
}

bool isSingleCompleteType(const QString &sig)
{
    return splitTypes(sig).size() == 1;
}

bool isDict(const QString &sig)
{
    return sig.startsWith(QLatin1String("a{"));
}

QChar dictKey(const QString &sig)
{
    return sig.at(2);
}

QString dictValue(const QString &sig)
{
    return sig.mid(3, sig.size() - 4);
}

QStringList structFields(const QString &sig)
{
    return splitTypes(sig.mid(1, sig.size() - 2));
}

// ------------------------------------------------------------ element types

// A struct whose fields QtDBus marshals in order. Qt 6.4's QDBusArgument has
// no std::tuple support, so the curated struct types are built on this.
template <typename... T>
struct DBusStruct {
    std::tuple<T...> fields;
};

template <typename... T>
QDBusArgument &operator<<(QDBusArgument &argument, const DBusStruct<T...> &value)
{
    argument.beginStructure();
    std::apply([&argument](const auto &...field) { (argument << ... << field); }, value.fields);
    argument.endStructure();
    return argument;
}

template <typename... T>
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusStruct<T...> &value)
{
    argument.beginStructure();
    std::apply([&argument](auto &...field) { (argument >> ... >> field); }, value.fields);
    argument.endStructure();
    return argument;
}

// The element types beginArray() and beginMap() accept: one registered
// QMetaType per array element / map value signature. QtDBus knows the native
// ones; the curated ones are registered here once (thread-safe static), also
// those recent Qt versions register themselves but Qt 6.4 does not. Written
// out on purpose: QDBusMetaType::typeToSignature is internal API. Extend case
// by case, with a test.
QMetaType elementMetaType(const QString &sig)
{
    static const QHash<QString, QMetaType> table = [] {
        QHash<QString, QMetaType> types{
            {QStringLiteral("y"), QMetaType::fromType<uchar>()},
            {QStringLiteral("b"), QMetaType::fromType<bool>()},
            {QStringLiteral("n"), QMetaType::fromType<short>()},
            {QStringLiteral("q"), QMetaType::fromType<ushort>()},
            {QStringLiteral("i"), QMetaType::fromType<int>()},
            {QStringLiteral("u"), QMetaType::fromType<uint>()},
            {QStringLiteral("x"), QMetaType::fromType<qlonglong>()},
            {QStringLiteral("t"), QMetaType::fromType<qulonglong>()},
            {QStringLiteral("d"), QMetaType::fromType<double>()},
            {QStringLiteral("s"), QMetaType::fromType<QString>()},
            {QStringLiteral("o"), QMetaType::fromType<QDBusObjectPath>()},
            {QStringLiteral("g"), QMetaType::fromType<QDBusSignature>()},
            {QStringLiteral("v"), QMetaType::fromType<QDBusVariant>()},
            {QStringLiteral("as"), QMetaType::fromType<QStringList>()},
            {QStringLiteral("ay"), QMetaType::fromType<QByteArray>()},
            {QStringLiteral("av"), QMetaType::fromType<QVariantList>()},
            {QStringLiteral("a{sv}"), QMetaType::fromType<QVariantMap>()},
            {QStringLiteral("ao"), QMetaType::fromType<QList<QDBusObjectPath>>()},
            {QStringLiteral("ag"), QMetaType::fromType<QList<QDBusSignature>>()},
            {QStringLiteral("ab"), QMetaType::fromType<QList<bool>>()},
            {QStringLiteral("an"), QMetaType::fromType<QList<short>>()},
            {QStringLiteral("aq"), QMetaType::fromType<QList<ushort>>()},
            {QStringLiteral("ai"), QMetaType::fromType<QList<int>>()},
            {QStringLiteral("au"), QMetaType::fromType<QList<uint>>()},
            {QStringLiteral("ax"), QMetaType::fromType<QList<qlonglong>>()},
            {QStringLiteral("at"), QMetaType::fromType<QList<qulonglong>>()},
            {QStringLiteral("ad"), QMetaType::fromType<QList<double>>()},
        };
        types.insert(QStringLiteral("a{ss}"), qDBusRegisterMetaType<QMap<QString, QString>>());
        types.insert(QStringLiteral("aas"), qDBusRegisterMetaType<QList<QStringList>>());
        types.insert(QStringLiteral("aay"), qDBusRegisterMetaType<QList<QByteArray>>());
        types.insert(QStringLiteral("aa{sv}"), qDBusRegisterMetaType<QList<QVariantMap>>());
        types.insert(QStringLiteral("a{sa{sv}}"),
                     qDBusRegisterMetaType<QMap<QString, QVariantMap>>());
        types.insert(QStringLiteral("aai"), qDBusRegisterMetaType<QList<QList<int>>>());
        types.insert(QStringLiteral("(si)"), qDBusRegisterMetaType<DBusStruct<QString, int>>());
        types.insert(QStringLiteral("(ss)"),
                     qDBusRegisterMetaType<DBusStruct<QString, QString>>());
        types.insert(QStringLiteral("(sss)"),
                     qDBusRegisterMetaType<DBusStruct<QString, QString, QString>>());
        types.insert(QStringLiteral("(ii)"), qDBusRegisterMetaType<DBusStruct<int, int>>());
        types.insert(QStringLiteral("(ai)"), qDBusRegisterMetaType<DBusStruct<QList<int>>>());
        types.insert(QStringLiteral("(oa{sv})"),
                     qDBusRegisterMetaType<DBusStruct<QDBusObjectPath, QVariantMap>>());
        types.insert(QStringLiteral("(iss)"),
                     qDBusRegisterMetaType<DBusStruct<int, QString, QString>>());
        return types;
    }();
    return table.value(sig);
}

// ------------------------------------------------------------ basic values

// Basic types: the strict conversion of PR3a.
bool coerceBasic(const QJsonValue &value, QChar type, QVariant *out, QString *error)
{
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
            const QString signature = value.toString();
            if (signature.size() > 255 || (!signature.isEmpty() && splitTypes(signature).isEmpty())) {
                *error = QStringLiteral("%1 is not a valid D-Bus signature").arg(jsonText(value));
                return false;
            }
            *out = QVariant::fromValue(QDBusSignature(signature));
        }
        return true;
    case 'h':
        *error = QStringLiteral("unix fd arguments are not supported (they cannot travel over MCP)");
        return false;
    default:
        *error = QStringLiteral("'%1' is not a basic D-Bus type").arg(type);
        return false;
    }
}

// A JSON object key as the JSON value a map key of `type` is read from:
// numbers and booleans are written as text in JSON keys.
QJsonValue keyValue(const QString &key, QChar type)
{
    switch (type.toLatin1()) {
    case 'd': {
        bool ok = false;
        const double number = key.toDouble(&ok);
        return ok ? QJsonValue(number) : QJsonValue(key);
    }
    case 'b':
        if (key == QLatin1String("true"))
            return true;
        if (key == QLatin1String("false"))
            return false;
        return key;
    default:
        return key; // integers: parseInteger reads decimal strings
    }
}

// ------------------------------------------------------------ pass 1: check

QString prefixed(const QString &where, const QString &error)
{
    return where + QStringLiteral(": ") + error;
}

QString checkValue(const QJsonValue &value, const QString &sig, int depth);

// What a variant may hold: {"@dbus": "<type>", "value": …} for an explicit
// type, otherwise the natural mapping of the JSON value.
QString checkVariant(const QJsonValue &value, int depth)
{
    if (depth > kMaxDepth)
        return QStringLiteral("nested deeper than %1 levels").arg(kMaxDepth);
    const QJsonObject object = value.toObject();
    if (value.isObject() && object.contains(QStringLiteral("@dbus"))) {
        const QJsonValue inner = object.value(QStringLiteral("@dbus"));
        if (!isSingleCompleteType(inner.toString()))
            return QStringLiteral("%1 is not a single complete D-Bus type").arg(jsonText(inner));
        return checkValue(object.value(QStringLiteral("value")), inner.toString(), depth + 1);
    }
    switch (value.type()) {
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QString error = checkVariant(array.at(i), depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("[%1]").arg(i), error);
        }
        return QString();
    }
    case QJsonValue::Object:
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString error = checkVariant(it.value(), depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("['%1']").arg(it.key()), error);
        }
        return QString();
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return QStringLiteral("null cannot be sent in a D-Bus variant");
    default:
        return QString();
    }
}

// Pass 1: checks `value` against the complete type `sig`; returns the error,
// empty when the value can be written. Builds nothing.
QString checkValue(const QJsonValue &value, const QString &sig, int depth)
{
    if (depth > kMaxDepth)
        return QStringLiteral("nested deeper than %1 levels").arg(kMaxDepth);
    const QChar type = sig.at(0);
    if (sig.size() == 1 && isBasicType(type)) {
        QVariant unused;
        QString error;
        return coerceBasic(value, type, &unused, &error) ? QString() : error;
    }
    if (sig == QLatin1String("v"))
        return checkVariant(value, depth + 1);
    if (sig == QLatin1String("ay")) {
        QVariant unused;
        QString error;
        return coerceByteArray(value, &unused, &error) ? QString() : error;
    }
    if (sig == QLatin1String("as")) {
        bool ok = value.isArray();
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            ok = ok && item.isString();
        return ok ? QString()
                  : QStringLiteral("expected an array of strings, got %1").arg(jsonText(value));
    }
    if (isDict(sig)) {
        const QChar key = dictKey(sig);
        const QString valueSig = dictValue(sig);
        if (key == QLatin1Char('h'))
            return QStringLiteral(
                "unix fd arguments are not supported (they cannot travel over MCP)");
        if (!elementMetaType(valueSig).isValid())
            return QStringLiteral("unsupported D-Bus signature '%1' (map value)").arg(valueSig);
        if (!value.isObject())
            return QStringLiteral("expected an object for '%1', got %2").arg(sig, jsonText(value));
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            QVariant unused;
            QString error;
            if (!coerceBasic(keyValue(it.key(), key), key, &unused, &error))
                return prefixed(QStringLiteral("key '%1'").arg(it.key()), error);
            error = checkValue(it.value(), valueSig, depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("['%1']").arg(it.key()), error);
        }
        return QString();
    }
    if (type == QLatin1Char('a')) {
        const QString element = sig.mid(1);
        if (!elementMetaType(element).isValid())
            return QStringLiteral("unsupported D-Bus signature '%1' (array element)").arg(element);
        if (!value.isArray())
            return QStringLiteral("expected an array for '%1', got %2").arg(sig, jsonText(value));
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QString error = checkValue(array.at(i), element, depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("[%1]").arg(i), error);
        }
        return QString();
    }
    // A struct: a JSON array with one item per field.
    const QStringList fields = structFields(sig);
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != fields.size())
        return QStringLiteral("expected an array of %1 fields for '%2', got %3")
            .arg(fields.size())
            .arg(sig, jsonText(value));
    for (int i = 0; i < fields.size(); ++i) {
        const QString error = checkValue(array.at(i), fields.at(i), depth + 1);
        if (!error.isEmpty())
            return prefixed(QStringLiteral("field %1").arg(i), error);
    }
    return QString();
}

// ------------------------------------------------------------ pass 2: build
// Only after checkValue() succeeded: nothing below can fail, so every
// begin*() meets its end*() (libdbus aborts the whole process when an
// array's content does not match the element type given to beginArray()).

QVariant basicValue(const QJsonValue &value, QChar type)
{
    QVariant out;
    QString unused;
    coerceBasic(value, type, &out, &unused);
    return out;
}

void appendBasic(QDBusArgument &argument, const QVariant &value, QChar type)
{
    switch (type.toLatin1()) {
    case 'y': argument << value.value<uchar>(); break;
    case 'b': argument << value.toBool(); break;
    case 'n': argument << value.value<short>(); break;
    case 'q': argument << value.value<ushort>(); break;
    case 'i': argument << value.value<int>(); break;
    case 'u': argument << value.value<uint>(); break;
    case 'x': argument << value.value<qlonglong>(); break;
    case 't': argument << value.value<qulonglong>(); break;
    case 'd': argument << value.toDouble(); break;
    case 's': argument << value.toString(); break;
    case 'o': argument << value.value<QDBusObjectPath>(); break;
    default: argument << value.value<QDBusSignature>(); break; // 'g'
    }
}

QVariant build(const QJsonValue &value, const QString &sig);

// The content of a variant: the explicit "@dbus" type, otherwise the natural
// mapping (scalars as DBusBridge::jsonToVariant maps them; arrays as 'av' and
// objects as 'a{sv}', whose items may carry "@dbus" again).
QVariant variantContent(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    if (value.isObject() && object.contains(QStringLiteral("@dbus")))
        return build(object.value(QStringLiteral("value")),
                     object.value(QStringLiteral("@dbus")).toString());
    if (value.isArray()) {
        QVariantList list;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            list.append(QVariant::fromValue(QDBusVariant(variantContent(item))));
        return list;
    }
    if (value.isObject()) {
        QVariantMap map;
        for (auto it = object.begin(); it != object.end(); ++it)
            map.insert(it.key(), variantContent(it.value()));
        return map;
    }
    return DBusBridge::jsonToVariant(value);
}

void write(QDBusArgument &argument, const QJsonValue &value, const QString &sig)
{
    const QChar type = sig.at(0);
    if (sig.size() == 1 && isBasicType(type)) {
        appendBasic(argument, basicValue(value, type), type);
    } else if (sig == QLatin1String("v")) {
        argument << QDBusVariant(variantContent(value));
    } else if (sig == QLatin1String("ay")) {
        argument << build(value, sig).toByteArray();
    } else if (isDict(sig)) {
        const QChar key = dictKey(sig);
        const QString valueSig = dictValue(sig);
        argument.beginMap(elementMetaType(QString(key)), elementMetaType(valueSig));
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            argument.beginMapEntry();
            appendBasic(argument, basicValue(keyValue(it.key(), key), key), key);
            write(argument, it.value(), valueSig);
            argument.endMapEntry();
        }
        argument.endMap();
    } else if (type == QLatin1Char('a')) {
        const QString element = sig.mid(1);
        argument.beginArray(elementMetaType(element));
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            write(argument, item, element);
        argument.endArray();
    } else {
        const QStringList fields = structFields(sig);
        const QJsonArray array = value.toArray();
        argument.beginStructure();
        for (int i = 0; i < fields.size(); ++i)
            write(argument, array.at(i), fields.at(i));
        argument.endStructure();
    }
}

// Pass 2 at the root: typed QVariants where QtDBus has a native type,
// otherwise a QDBusArgument written by hand.
QVariant build(const QJsonValue &value, const QString &sig)
{
    const QChar type = sig.at(0);
    if (sig.size() == 1 && isBasicType(type))
        return basicValue(value, type);
    if (sig == QLatin1String("v"))
        return QVariant::fromValue(QDBusVariant(variantContent(value)));
    if (sig == QLatin1String("ay")) {
        QVariant out;
        QString unused;
        coerceByteArray(value, &out, &unused);
        return out;
    }
    if (sig == QLatin1String("as")) {
        QStringList strings;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            strings.append(item.toString());
        return strings;
    }
    QDBusArgument argument;
    write(argument, value, sig);
    return QVariant::fromValue(argument);
}
```

et remplacer tout le bloc `namespace TypeCoercer { … }` par :

```cpp
namespace TypeCoercer {

bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error)
{
    if (!isSingleCompleteType(signature)) {
        *error = QStringLiteral("'%1' is not a single complete D-Bus type").arg(signature);
        return false;
    }
    const QString problem = checkValue(value, signature, 0);
    if (!problem.isEmpty()) {
        *error = problem;
        return false;
    }
    *out = build(value, signature);
    return true;
}

} // namespace TypeCoercer
```

(L'ancienne branche `as`/`ay`/repli `jsonToVariant` de `coerce` disparaît : tout passe par `checkValue`/`build`.)

- [ ] **Step 5: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS (`test_coercion` : classes `Coercion`, `Containers`, `Variants`, `NaturalMapping`). Si un attendu ne diffère que par un détail de `repr` de dbus-python (ordre de clés d'un dictionnaire, par exemple), comparer au résultat de la matrice du spike (`validation/spike-qt642/src-coerce/spike/matrix.out`, stratégie `curated`) et consigner une ligne `Ruling:`.

- [ ] **Step 6: Commit**

```bash
git add src/dbus/typecoercer.h src/dbus/typecoercer.cpp tests/fixtures/echo_service.py tests/test_coercion.py
git commit -s -m "Convert container and variant arguments from the declared signature (M2, M6)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: `Properties.Set` typé d'après la propriété déclarée

**Files:**
- Modify: `src/dbus/interfaceresolver.h`, `src/dbus/interfaceresolver.cpp`, `src/dbus/dbusbridge.cpp` (`callMethod`), `tests/fixtures/echo_service.py`, `tests/test_coercion.py`

**Interfaces:**
- Consumes: `TypeCoercer::coerce` avec `@dbus` (Task 3) ; `MethodResolution`, `resolveMethod` (PR3a/PR6).
- Produces: `QString MethodResolution::introspection` (XML de l'introspection réussie) ; `QString propertyTypeFromXml(const QString &xml, const QString &interface, const QString &property)` ; objet de fixture `/Props` (`Properties.Set(ssv)`, `org.plasmamcp.Props.LastSet() -> s`, propriétés `Volume: d`, `Level: y`).

- [ ] **Step 1: Fixture et tests qui échouent**

Dans `tests/fixtures/echo_service.py`, après la classe `Loose` :

```python
PROPS_XML = '''<node>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Set">
      <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/>
    </method>
  </interface>
  <interface name="org.plasmamcp.Props">
    <method name="LastSet"><arg type="s" direction="out"/></method>
    <property name="Volume" type="d" access="readwrite"/>
    <property name="Level" type="y" access="readwrite"/>
  </interface>
  <node name="child">
    <interface name="org.plasmamcp.Props">
      <property name="Level" type="s" access="readwrite"/>
    </interface>
  </node>
</node>'''


class Props(dbus.service.Object):
    """Properties.Set whose property types exist only in the introspection
    data (dbus-python does not declare properties itself)."""
    last_set = 'never'

    @dbus.service.method('org.freedesktop.DBus.Introspectable', in_signature='',
                         out_signature='s')
    def Introspect(self):
        return PROPS_XML

    @dbus.service.method(PROPS, in_signature='ssv', out_signature='', message_keyword='msg')
    def Set(self, iface, prop, value, msg=None):
        Props.last_set = '%s|%s.%s=%r' % (msg.get_signature(), iface, prop, value)

    @dbus.service.method('org.plasmamcp.Props', in_signature='', out_signature='s')
    def LastSet(self):
        return Props.last_set
```

et dans `main()`, après `Loose(bus, '/Loose')` : `Props(bus, '/Props')`. Docstring : `- /Props: Properties.Set typed by <property> declarations; LastSet() tells what arrived.`

Dans `tests/test_coercion.py`, avant `class NaturalMapping` :

```python
PROPS_OBJECT = {'service': ECHO['service'], 'path': '/Props'}


class PropertiesSet(FixtureTestCase):

    def set_property(self, prop, value, interface=True):
        bridge = self.bridge()
        arguments = dict(PROPS_OBJECT, method='Set', args=['org.plasmamcp.Props', prop, value])
        if interface:
            arguments['interface'] = 'org.freedesktop.DBus.Properties'
        reply = bridge.call('dbus_call', arguments)
        if reply.is_error:
            return reply
        return bridge.call('dbus_call', dict(PROPS_OBJECT, interface='org.plasmamcp.Props',
                                             method='LastSet'))

    def test_declared_double(self):
        self.assertEqual(self.set_property('Volume', 1),
                         ('ssv|org.plasmamcp.Props.Volume=dbus.Double(1.0, variant_level=1)',
                          False))

    def test_declared_byte(self):
        self.assertEqual(self.set_property('Level', 2),
                         ('ssv|org.plasmamcp.Props.Level=dbus.Byte(2, variant_level=1)', False))

    def test_without_interface(self):
        self.assertEqual(self.set_property('Level', 2, interface=False),
                         ('ssv|org.plasmamcp.Props.Level=dbus.Byte(2, variant_level=1)', False))

    def test_declared_type_is_strict(self):
        self.assertEqual(self.set_property('Level', 300),
                         ('argument 2 (v): 300 out of range [0,255]', True))

    def test_explicit_type_wins(self):
        self.assertEqual(self.set_property('Level', {'@dbus': 'u', 'value': 2}),
                         ('ssv|org.plasmamcp.Props.Level=dbus.UInt32(2, variant_level=1)', False))

    def test_undeclared_property_is_natural(self):
        self.assertEqual(self.set_property('Other', 2),
                         ('ssv|org.plasmamcp.Props.Other=dbus.Int32(2, variant_level=1)', False))
```

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_coercion.PropertiesSet; cd ..`
Expected: FAIL `test_declared_double` (`Int32(1)`), `test_declared_byte`, `test_without_interface` (`Int32(2)`), `test_declared_type_is_strict` (envoyé) ; PASS `test_explicit_type_wins`, `test_undeclared_property_is_natural`.

- [ ] **Step 2: Lire le type déclaré**

`src/dbus/interfaceresolver.h` — dans `MethodResolution`, après `inSignature` :

```cpp
    // The introspection XML, when the introspection succeeded.
    QString introspection;
```

et après `resolveMethodFromXml` :

```cpp
// The D-Bus type of `property` on `interface`, from the object's own
// introspection data (child node descriptions ignored); empty when the
// property is not declared.
QString propertyTypeFromXml(const QString &xml, const QString &interface,
                            const QString &property);
```

`src/dbus/interfaceresolver.cpp` — dans `resolveMethod`, remplacer la dernière ligne `return resolveMethodFromXml(reply.arguments().first().toString(), interface, method);` par :

```cpp
    const QString xml = reply.arguments().first().toString();
    MethodResolution result = resolveMethodFromXml(xml, interface, method);
    result.introspection = xml;
    return result;
```

et ajouter :

```cpp
QString propertyTypeFromXml(const QString &xml, const QString &interface,
                            const QString &property)
{
    QString currentInterface;
    int nodeDepth = 0; // 1 = the introspected object; deeper = child descriptions
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("node")) {
            ++nodeDepth;
        } else if (reader.isEndElement() && reader.name() == QLatin1String("node")) {
            --nodeDepth;
        } else if (nodeDepth > 1) {
            continue;
        } else if (reader.isStartElement()) {
            const QXmlStreamAttributes attributes = reader.attributes();
            if (reader.name() == QLatin1String("interface"))
                currentInterface = attributes.value(QLatin1String("name")).toString();
            else if (reader.name() == QLatin1String("property") && currentInterface == interface
                     && attributes.value(QLatin1String("name")) == property)
                return attributes.value(QLatin1String("type")).toString();
        } else if (reader.isEndElement() && reader.name() == QLatin1String("interface")) {
            currentInterface.clear();
        }
    }
    return QString();
}
```

- [ ] **Step 3: Typer le `v`**

`src/dbus/dbusbridge.cpp`, `callMethod` : juste après le calcul de `typed`, ajouter :

```cpp
    // Properties.Set(s, s, v): the variant takes the type the target interface
    // declares for the property, unless the caller forced one with "@dbus".
    QJsonArray callArgs = args;
    if (typed && resolution.interface == QLatin1String("org.freedesktop.DBus.Properties")
        && method == QLatin1String("Set")
        && resolution.inSignature
            == QStringList{QStringLiteral("s"), QStringLiteral("s"), QStringLiteral("v")}
        && !(args.at(2).isObject()
             && args.at(2).toObject().contains(QStringLiteral("@dbus")))) {
        const QString type = propertyTypeFromXml(resolution.introspection,
                                                 args.at(0).toString(), args.at(1).toString());
        if (!type.isEmpty())
            callArgs[2] = QJsonObject{{QStringLiteral("@dbus"), type},
                                      {QStringLiteral("value"), args.at(2)}};
    }
```

et, dans la boucle qui suit, remplacer chaque `args.at(i)` par `callArgs.at(i)` (deux occurrences : `jsonToVariant(args.at(i))` et `TypeCoercer::coerce(args.at(i), …)`).

- [ ] **Step 4: Vérifier le succès**

Run: `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error`
Expected: build sans warning ; tous les modules PASS.

- [ ] **Step 5: Commit**

```bash
git add src/dbus/interfaceresolver.h src/dbus/interfaceresolver.cpp src/dbus/dbusbridge.cpp tests/fixtures/echo_service.py tests/test_coercion.py
git commit -s -m "Type Properties.Set's value from the declared property type

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Fuzz borné, documentation, spec, contrôle Qt 6.4.2

**Files:**
- Modify: `tests/test_coercion.py`, `README.md`, `CLAUDE.md`, `CHANGELOG.md`, `docs/superpowers/specs/2026-10-01-remediation-core-design.md`

**Interfaces:**
- Consumes: `EchoV` (fixture), coercer complet (Tasks 3-4).
- Produces: classe `Fuzz` ; documentation définitive.

- [ ] **Step 1: Test de robustesse**

Dans `tests/test_coercion.py`, ajouter `import random` et `import dbus` aux imports, puis avant `if __name__ == '__main__':` :

```python
def random_signature(rng, depth=0):
    roll = rng.random()
    if depth >= 3 or roll < 0.4:
        return rng.choice('ybnqiuxtdsogvh')
    if roll < 0.6:
        return 'a' + random_signature(rng, depth + 1)
    if roll < 0.75:
        return 'a{' + rng.choice('sybnqiuxtdog') + random_signature(rng, depth + 1) + '}'
    return '(' + ''.join(random_signature(rng, depth + 1)
                         for _ in range(rng.randint(1, 3))) + ')'


def random_json(rng, depth=0):
    roll = rng.random()
    if depth >= 3 or roll < 0.5:
        return rng.choice([0, -1, 300, 2 ** 31, 2 ** 63 - 1, 1.5, '', 'x', '/a', 'aGk=',
                           'a{sv}', True, None])
    if roll < 0.75:
        return [random_json(rng, depth + 1) for _ in range(rng.randint(0, 3))]
    return {rng.choice(['k', '1', 'true', '/p', '@dbus']): random_json(rng, depth + 1)
            for _ in range(rng.randint(0, 3))}


def value_for(rng, sig):
    """A JSON value of the right shape for `sig`, so the build path runs too."""
    c = sig[0]
    if c in 'ynqu':
        return rng.randint(0, 255)
    if c in 'ix':
        return rng.randint(-1000, 1000)
    if c == 't':
        return rng.randint(0, 1000)
    if c == 'b':
        return rng.random() < 0.5
    if c == 'd':
        return rng.random()
    if c in 'sh':
        return 'x'
    if c == 'o':
        return '/a/b'
    if c == 'g':
        return 'a{sv}'
    if c == 'v':
        return rng.choice([1, 'x', [1, 2], {'k': True}])
    if sig.startswith('a{'):
        keys = {'b': 'true', 'd': '1.5', 'o': '/k', 'g': 's', 's': 'k'}
        return {keys.get(sig[2], '1'): value_for(rng, sig[3:-1])
                for _ in range(rng.randint(0, 2))}
    if c == 'a':
        return [value_for(rng, sig[1:]) for _ in range(rng.randint(0, 2))]
    return [value_for(rng, field) for field in dbus.Signature(sig[1:-1])]


class Fuzz(FixtureTestCase):
    """Bounded fuzz (fixed seed): the bridge never dies, every call answers."""

    def test_random_signatures_and_values(self):
        rng = random.Random(20261001)
        session = self.bridge()
        successes = 0
        for n in range(400):
            if rng.random() < 0.1:
                sig = ''.join(rng.choice('ab{}()vsix') for _ in range(rng.randint(1, 6)))
                value = random_json(rng)
            else:
                sig = random_signature(rng)
                value = value_for(rng, sig) if rng.random() < 0.5 else random_json(rng)
            with self.subTest(n=n, sig=sig):
                reply = session.call('dbus_call', dict(ECHO, method='EchoV',
                                                       args=[{'@dbus': sig, 'value': value}]))
                if not reply.is_error:
                    successes += 1
                    self.assertTrue(reply.text.startswith('v|'), reply.text)
        self.assertIsNone(session.proc.poll())
        self.assertGreater(successes, 40)
```

Run: `cmake --build build && cd tests && PLASMA_MCP_BRIDGE=$PWD/../build/bin/plasma-mcp-bridge ./run_with_bus.sh python3 -m unittest -v test_coercion.Fuzz; cd ..`
Expected: PASS (filet de sécurité : s'il échoue, c'est un défaut du coercer à corriger par superpowers:systematic-debugging, avec un test ciblé, jamais en affaiblissant le fuzz).

- [ ] **Step 2: README**

Remplacer le paragraphe des réponses :

```markdown
A method without return value — like `nextDesktop` above — replies `null`.
Replies are marshalled back to JSON, including arrays, structs and maps:
numbers are exact (an integer beyond ±2^53, `int64` or `uint64`, comes back as
a decimal string), byte arrays (`ay`) come back as base64 strings, a unix file
descriptor (`h`) as `"<unix fd: not transferable over MCP>"`, and a value of a
type that cannot be represented is replaced by
`"<unsupported D-Bus type '<signature>'>"`.
```

et le paragraphe des arguments :

```markdown
Arguments are converted to the types the method declares in its introspection
data — also when `interface` is omitted, as long as the method name is unique
on the object — and checked in full before anything is sent:

- Integers are range-checked (pass a `uint64` beyond the int64 range as a
  decimal string); `ay` accepts an array of bytes or a base64 string; `o` and
  `g` must be a valid object path / signature.
- Arrays are JSON arrays, maps (`a{…}`) JSON objects (numeric and boolean keys
  as text), structs JSON arrays with one item per field. Array elements and map
  values may be basic types, `v`, `as`, `ay`, `av`, `a{sv}`, `ao`, `ag`, arrays
  of integers, booleans or doubles, `a{ss}`, `aas`, `aay`, `aai`, `aa{sv}`,
  `a{sa{sv}}`, and the structs `(si)`, `(ss)`, `(sss)`, `(ii)`, `(ai)`,
  `(oa{sv})`, `(iss)`; anything else is refused with
  `unsupported D-Bus signature '…'`.
- A variant (`v`) takes the natural type of the JSON value — boolean `b`,
  integer `i` (or `x` beyond 32 bits), other number `d`, string `s`, array
  `av`, object `a{sv}` — unless it is written
  `{"@dbus": "<type>", "value": …}`, e.g. `{"@dbus": "y", "value": 2}` for a
  notification's `urgency` hint. `Properties.Set` gives its value the type the
  interface declares for the property.
- Unix file descriptors (`h`) cannot travel over MCP and are refused.

When the method cannot be resolved (no introspection data, or the name is
declared by several interfaces and `interface` is omitted), arguments take the
natural types above and the service itself rejects a mismatch.
```

- [ ] **Step 3: CLAUDE.md, CHANGELOG**

`CLAUDE.md` : dans la description de `src/dbus/`, remplacer `(\`dbus/typecoercer.*\`; strict for basic types, \`as\`, \`ay\`)` par `(\`dbus/typecoercer.*\`: driven by the parsed signature, checked in full before anything is built — libdbus aborts on a malformed array; element types come from a written-out table, extended case by case with a test)`.

`CHANGELOG.md`, sous `## 0.2.0 (unreleased)` — à `### Fixed` :

```markdown
- Container arguments (`au`, `a{ss}`, `(si)`, `a(si)`, `aas`, `a{sa{sv}}`, …)
  and variants are sent with the declared type instead of `av`/`a{sv}`;
  `Properties.Set` sends the property's declared type.
- A unix fd in a reply reads `"<unix fd: not transferable over MCP>"` (was
  `<unrepresentable:QDBusUnixFileDescriptor>`).
```

à `### Changed` :

```markdown
- An integer in a variant, or in an argument whose type is unknown, is sent as
  `i` when it fits in 32 bits (was always `x`).
- An `int64` beyond ±2^53 is returned as a decimal string, like a `uint64`.
```

à `### Added` :

```markdown
- `{"@dbus": "<type>", "value": …}` forces the type of a variant argument.
```

- [ ] **Step 4: Spec**

Dans la spec, §4.3 :
- dans la table des types d'élément, ligne des structs curées, remplacer `` `std::tuple<…>` (`operator<<` public de `qdbusargument.h`) `` par `` `DBusStruct<T…>` interne (Qt 6.4.2 n'a pas le support `std::tuple` de `qdbusargument.h` — amendement PR3b) `` ;
- ajouter à la fin du paragraphe **`h` (m2)** : « En entrée, aussi comme clé de map. »
§9 : remplacer `Nombres exacts (et \`t\` > 2^53 en chaîne)` par `Nombres exacts (\`t\` et \`x\` hors ±2^53 en chaîne — décision PR3b)`.

- [ ] **Step 5: Vérifier en local et en Qt 6.4.2**

Run (local) : `cmake --build build && ctest --test-dir build --output-on-failure --no-tests=error && ./build/bin/plasma-mcp-bridge --emit-skill 2>/dev/null | sha256sum`
Expected: tous les modules PASS ; l'empreinte de `--emit-skill` est identique à celle de `main` (aucune description d'outil ne change : la calculer aussi sur un build de `main` si besoin).

Run (Qt 6.4.2 ; un script dans un fichier si l'environnement refuse les `bash -c` en ligne) :
```bash
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/src -w /src plasma-mcp-ci:noble bash -c '
  cmake -S /src -B /tmp/b -G Ninja -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror" -DPython3_EXECUTABLE=/usr/bin/python3 >/tmp/c.log &&
  cmake --build /tmp/b >/tmp/b.log 2>&1; echo "build=$?"; grep -c "warning:" /tmp/b.log; ctest --test-dir /tmp/b --output-on-failure --no-tests=error'
```
Expected: `build=0`, `0` warning, tous les modules PASS — en particulier `test_coercion` (le `DBusStruct` remplace `std::tuple` absent en 6.4.2).

- [ ] **Step 6: Commit**

```bash
git add tests/test_coercion.py README.md CLAUDE.md CHANGELOG.md docs/superpowers/specs/2026-10-01-remediation-core-design.md
git commit -s -m "docs: argument conversion rules, @dbus, h; bounded fuzz of the coercer (d5, d9)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Fin de la PR3b

- [ ] `git log --oneline main..` montre 6 commits (ce plan + 5 tâches), tous signés ; `git status` propre.
- [ ] Ne pas pousser ni ouvrir la PR sans l'accord de l'utilisateur. La PR (base `main`) liste M2, M6, m2, m12, d5, d9, les deux écarts à la spec, et se termine par `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- [ ] Après merge : bump du sous-module enterprise (aucune dérive de skill attendue) ; la prose de la skill (PR7) peut documenter `@dbus`.
