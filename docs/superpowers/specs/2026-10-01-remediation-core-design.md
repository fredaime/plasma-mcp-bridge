# Remediation du core plasma-mcp-bridge — design

- Date : 2026-10-01
- Statut : proposé, en attente de relecture
- Base : `main` @ `1f3752e`
- Version cible : 0.2.0 (changements de comportement visibles par les clients, voir §9)

## 1. Contexte et objectif

Une validation en conditions réelles (KDE Plasma 6.7.5 sous Wayland, Qt 6.11.2, Ubuntu 26.10,
bus de session dbus-broker) a comparé les promesses du README et du CLAUDE.md au comportement du
binaire. Elle a relevé des écarts allant du blocage complet du serveur à de simples imprécisions de
documentation. Une revue adversariale du plan de correction, des prototypes jetables, et une
re-compilation en Qt 6.4.2 (version de la CI) ont ensuite validé les choix ci-dessous.

**Objectif** : que chaque promesse documentée soit tenue, ou retirée de la documentation, sans
nouvelle dépendance (Qt 6 Core + DBus uniquement) et sans casser l'ABI de plugins
`org.kde.plasma.mcpbridge.PluginInterface/1.0`.

**Hors périmètre** : nouvelles capacités (injection d'entrées, capture d'écran, réception de signaux
D-Bus, et donc des réponses de `org.freedesktop.portal.Request`). Elles restent dans la roadmap et la
documentation le dit explicitement. Exception décidée : le support de la révision MCP 2026-07-28
(sous-projet F).

## 2. Écarts traités

Les identifiants sont ceux du rapport de validation. Sévérité : B = bloquant, M = majeur, m = mineur,
d = documentation.

| Id | Écart constaté | Sous-projet |
|---|---|---|
| B1 | Une réponse contenant un `as` ou un `ay` imbriqué dans un tableau ou une structure (`aas`, `(as)`, `aay`) fait boucler le démarshalleur à l'infini (100 % CPU, ~65 Mo/s de RSS) ; le serveur mono-thread ne répond plus. En valeur de map (`a{sas}`, `a{say}`), la valeur devient `""`. Cas réel : `org.kde.kglobalaccel.allMainComponents`. | A0 |
| M7 | Une map dont la clé est `o` ou `g` perd sa clé (`a{oas}` → `{"": …}`). | A0 |
| M1 | Les réponses numériques scalaires passent par `QString::number(double)` (précision 6) : `123456789` → `1.23457e+08`, `3.14159265` → `3.14159`. Touche les positions MPRIS, PID, timestamps. | A1 |
| M2 | La coercition des arguments ne sait pas produire `au`, `a{ss}`, `(si)`, `ay` (depuis un tableau d'entiers), `o`, `v` : la signature mise sur le fil est fausse (`av`, `a{sv}`, `s`, `d`…). `Properties.Set` part en `ssd` et `SetPosition(o,x)` en `sx`, rejetés par les services stricts. | A2 |
| M6 | Débordements et arrondis silencieux : `-1`→`u` = 4294967295, `5000000000`→`u` = 705032704, `300`→`y` = 44, `2.7`→`n` = 3. `ay` en entrée n'accepte pas le base64 émis en sortie. | A1, A2 |
| m1 | Une méthode `void` appelée avec `interface` renvoie le texte `<unrepresentable:>` au lieu de `null`. | A1 |
| m2 | Un `h` (descripteur de fichier) en réponse produit `<unrepresentable:QDBusUnixFileDescriptor>`. | A2 |
| m4 | `interface: "org.freedesktop.DBus"` échoue (« No such interface ») : `QDBusAbstractInterface::isValid()` repose sur le suivi du propriétaire, désactivé pour le démon de bus. | A1 |
| M5 | Aucun garde-fou : le bus système est joignable par défaut, `PowerOff` ou `KWin.Scripting` passeraient sans friction. | C |
| m5 | Une valeur de `bus` inconnue (`"sytem"`) retombe silencieusement sur le bus de session. | C |
| m6 | Le fichier d'activation D-Bus installé lance un serveur stdio sans client, qui sort aussitôt. | C |
| m7 | Un `--plugin` introuvable n'est qu'un warning ; le bridge démarre avec code 0. | C |
| M4 | Serveur mono-thread et bloquant : pendant un appel lent, `ping` attend ; timeout fixe de 25 s ; `notifications/cancelled` ignoré. | D |
| m3 | Frames JSON invalides et batchs ignorés sans erreur `-32700`/`-32600` ; log trompeur (« no error occurred »). | D |
| m8 | Une seule version MCP (`2024-11-05`) ; des révisions plus récentes existent. | D, F |
| m11 | `SIGPIPE` tue le bridge si le client ferme stdout pendant un appel. | D |
| m13 | Deux outils de même nom peuvent être enregistrés sans erreur. | D |
| m12 | Avec la coercition, un entier JSON placé dans un `v` part en `i` (au lieu de `x`) s'il tient sur 32 bits — changement documenté. | A2 |
| d5, d9 | README : « Any reply is marshalled back to JSON » et l'exemple `nextDesktop` sont contredits par B1/m1/m2. | A1, A2 |
| d8 | README : l'install pose aussi la bibliothèque, les en-têtes et le paquet CMake. | C |
| D15 | README : « portals » présentés comme pilotables, alors que leurs réponses arrivent par signal. | C |

## 3. Socle de tests (livré avec A0)

Il n'existe aucune suite de tests. Chaque correctif ci-dessous arrive avec ses tests dans la même PR ;
le rouge-avant-vert reste local (TDD) et la CI ne voit que du vert.

### 3.1 Composants

| Fichier | Rôle |
|---|---|
| `tests/CMakeLists.txt` | Branché par `if(BUILD_TESTING) add_subdirectory(tests) endif()` (`BUILD_TESTING` est fourni par `KDECMakeSettings`). Cherche Python 3 et vérifie `import dbus, gi` ; si absent : `message(WARNING …)` puis `return()` — jamais `FATAL_ERROR`, pour qu'un consommateur qui configure le core sans ces dépendances ne casse pas. Un `add_test` par module, `TIMEOUT 120`, chemin du binaire passé par variable d'environnement. |
| `tests/fixtures/session.conf` | Configuration de bus de session privée : `unix:tmpdir`, auth `EXTERNAL`, policy permissive, **aucun `servicedir`** (sinon une centaine de services sont activables pendant les tests, dont certains qui ne répondent jamais sans bureau). |
| `tests/run_with_bus.sh` (mode 755) | `dbus-run-session --config-file=fixtures/session.conf -- …` ; exporte `DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"` (sinon `bus: "system"` atteint le vrai bus système), `LANG=C.UTF-8` (Qt 6.11 émet un warning de locale qui devient fatal sous `QT_FATAL_WARNINGS=1`). |
| `tests/fixtures/echo_service.py` | Oracle D-Bus (python3-dbus) : `Echo*` renvoient la signature reçue sur le fil et les valeurs ; `Ret*` renvoient des valeurs typées (`u`, `x`, `d`, `t`, `ay`, `h`, void, `aas`, `(as)`, `aay`, `a{sas}`, `a{say}`, `a{oas}`, …) ; `Slow(d)` **asynchrone** (`async_callbacks` + `GLib.timeout_add`) et `SlowBlocking(d)` ; `org.freedesktop.DBus.Properties.Set/Get` observables. |
| `tests/mcp_session.py` | Client MCP : lance le bridge (arguments supplémentaires possibles, par exemple `--plugin`), délai par appel (un dépassement est un **échec** de test et tue le bridge), `PR_SET_PDEATHSIG` sur les fils, refus de démarrer hors d'un bus privé (vérifie la variable posée par `run_with_bus.sh`), corrélation par `id`. |
| `tests/test_*.py` | `unittest` de la stdlib, un module par thème : `test_smoke`, `test_demarshall`, `test_coercion`, `test_policy`, `test_async`, `test_protocol`, `test_modern` (F). |

Conventions : assertions sur les **noms** d'erreur D-Bus (`org.freedesktop.DBus.Error.*`), jamais sur
leurs messages (ils diffèrent entre dbus-daemon et dbus-broker) ; chaque fixture attend l'acquisition
de son nom sur le bus avant de rendre la main.

### 3.2 Compilation et CI

- `add_compile_definitions(QT_WARN_DEPRECATED_UP_TO=0x060400 QT_DEPRECATED_WARNINGS_SINCE=0x060400)` :
  mêmes avertissements en 6.4 (CI) et 6.11 ; pas de `qAsConst`, `_qs`, `Q_FOREACH`.
  (`QT_WARN_DEPRECATED_UP_TO` est le nom Qt ≥ 6.5, `QT_DEPRECATED_WARNINGS_SINCE` celui de Qt 6.4.
  `QT_DISABLE_DEPRECATED_UP_TO` n'existe qu'à partir de Qt 6.5 : il n'est pas utilisé, les API
  dépréciées restent signalées par un avertissement, donc bloquées par `-Werror`.)
- CI `ubuntu-24.04` : ajouter `dbus-daemon python3-dbus python3-gi` et une étape
  `ctest --test-dir build --output-on-failure --no-tests=error` (une suite désactivée faute de
  dépendances ne doit pas donner une CI verte) ; le smoke test existant reste.
- Mesures de référence : build 29 s ; ctest du socle ≈ 2,4 s.

## 4. Sous-projet A — marshalling

### 4.1 A0 : hotfix B1 + M7 (mergeable seul)

Cause de B1 : `QDBusDemarshaller::currentType()` classe les tableaux d'octets et de chaînes en
`BasicType`. `extractBasic()` tombe alors dans sa branche `default`, lit une `QString` sans faire
avancer l'itérateur, et la boucle `while (!atEnd())` du conteneur parent ne se termine jamais.
Comportement identique en Qt 6.4.2 et 6.11.2.

Changements, tous dans `src/dbus/dbusbridge.cpp` (aucun en-tête installé modifié) :

1. `extractBasic()` traite `case 'a'` : `ay` → `QByteArray` (puis base64 via `variantToJson`), `as` →
   `QStringList`. Pour toute autre signature, il **ne lit rien** et renvoie un `QVariant` invalide.
2. Un helper interne `static bool demarshallElement(const QDBusArgument &, QJsonValue &out)` renvoie
   « élément consommé ». Les boucles tableau / structure / map **s'arrêtent** dès qu'un élément n'a pas
   été consommé et insèrent à sa place le marqueur `"<unsupported D-Bus type '<sig>'>"`. Un conteneur
   imbriqué déjà ouvert compte comme consommé (`begin*()` a fait avancer l'itérateur parent), donc le
   reste de la réponse est décodé.
3. M7 : la clé de map est convertie via `variantToJson()` (qui sait traiter `QDBusObjectPath` et
   `QDBusSignature`), et non via `QVariant::toString()`.

Tests (`test_demarshall`) : `aas`, `(as)`, `aay`, `a{sas}`, `a{say}`, `a{oas}`, `a{gas}`, tableaux
vides (`as`, `ay`, `a{sas}`), `ag`, `ao`, non-régression `a(iss)`, `aai`, `a(ai)`, `av` de `as`,
`a{sv}` ; chaque appel doit répondre en < 2 s.

Prototype vérifié : +75/−22 lignes, 205 symboles exportés identiques avant/après, 0 warning en
`-Werror` (6.4.2 et 6.11.2).

### 4.2 A1 : nombres, chemin d'appel unique, résolveur d'interface

**Nombres (M1).** `stringify()` écrit un entier JSON en `qint64` et un réel avec
`QString::number(d, 'g', QLocale::FloatingPointShortest)`. Dans `variantToJson()`, un `t` (uint64)
supérieur à 2^53 est émis comme **chaîne** décimale exacte, partout (scalaire ou imbriqué), pour ne pas
perdre de précision dans un double JSON.

**Chemin d'appel unique (m1, m4).** `DBusBridge::callMethod()` n'utilise plus `QDBusInterface` : il
construit l'appel avec `QDBusMessage::createMethodCall(service, path, interface, method)` et l'envoie
avec `QDBusConnection::call()`. Une réponse sans argument donne `null` (m1). L'appel au démon de bus
avec `interface: "org.freedesktop.DBus"` fonctionne (m4).

**Résolveur d'interface.** Avant chaque appel, le bridge introspecte l'objet (`Introspect`, ≈ 1 ms ;
**pas de cache**) et parse le XML avec `QXmlStreamReader`. Résultat à trois états :

| État | Condition | Effet |
|---|---|---|
| unique | `interface` fourni et la méthode y est déclarée, ou `interface` absent et le nom de méthode n'apparaît que dans une interface | signature d'entrée connue → coercition typée ; interface explicite sur le message |
| ambigu | `interface` absent et la méthode existe dans plusieurs interfaces | mapping naturel (§4.3) ; interface laissée vide |
| impossible | `Introspect` échoue, ou la méthode est absente du XML | mapping naturel ; l'interface fournie est quand même utilisée ; aucune erreur ajoutée |

Le résolveur vit dans `src/dbus/interfaceresolver.{h,cpp}` (en-tête **non installé**) : la policy de C
s'en sert aussi. Aucun en-tête installé ne change.

**Plages scalaires (M6, première partie).** Chaque type entier est borné (`y` 0–255, `n`, `q`, `i`,
`u`, `x`, `t`) ; un réel non entier vers un type entier est refusé ; message d'erreur
`argument <n> (<sig>): <valeur> out of range [<min>,<max>]` ou `… is not an integer`, renvoyé en
`isError` sans envoyer le message. `ay` accepte un tableau d'entiers bornés ou une chaîne base64
décodée strictement (`QByteArray::fromBase64Encoding` avec `AbortOnBase64DecodingErrors`).

Tests (`test_coercion`, partie A1) : nombres exacts (`u`, `x`, `d`, `t` > 2^53), void → `null` avec et
sans interface, `GetConnectionUnixProcessID` avec `interface: "org.freedesktop.DBus"`, les quatre
débordements de M6, `ay` tableau et base64, base64 invalide.

### 4.3 A2 : coercition des conteneurs (M2, M6, m2, m12)

Nouvelle unité `src/dbus/typecoercer.{h,cpp}` (en-tête non installé).

**Principe.** Un seul écrivain récursif, **piloté par la signature parsée** et jamais par les types
JSON, en deux passes :

1. **Validation** : parse complet de la signature (types complets, clés de map basiques, éléments
   supportés), puis vérification des valeurs JSON (forme, plages). Aucune construction tant qu'une
   erreur subsiste.
2. **Construction** : uniquement si la validation a réussi ; elle ne peut plus échouer, donc aucun
   `begin*()` ne reste sans son `end*()`.

Cette discipline est imposée par un fait vérifié : si le contenu écrit dans un tableau ne correspond
pas à la signature d'élément déclarée par `beginArray`, libdbus appelle `abort()` sur tout le
processus.

**Construction.** Basiques : `QVariant` typé. `o` → `QDBusObjectPath` (validé), `g` →
`QDBusSignature` (validée). `v` → `QDBusVariant(inner)`, où `inner` peut être un `QDBusArgument`.
Structures, tableaux et maps racines : `QDBusArgument` construit à la main (`beginStructure`,
`beginArray(QMetaType)`, `beginMap(QMetaType, QMetaType)`), puis `QVariant::fromValue(arg)`.

**Type d'élément.** Seul le type d'élément d'un tableau, ou de valeur d'une map, exige un `QMetaType`
enregistré. Table codée en dur, sans interroger `QDBusMetaType::typeToSignature` (API `\internal`) :

| Niveau | Signatures d'élément | Métatype |
|---|---|---|
| natif | `y b n q i u x t d s o g v` | types de base, `QDBusObjectPath`, `QDBusSignature`, `QDBusVariant` |
| natif | `as`, `ay`, `av`, `a{sv}`, `ao`, `ag`, `ab an aq ai au ax at ad` | `QStringList`, `QByteArray`, `QVariantList`, `QVariantMap`, `QList<QDBusObjectPath>`, `QList<QDBusSignature>`, `QList<T>` |
| curé | `a{ss}`, `aas`, `aay`, `aa{sv}`, `a{sa{sv}}`, `aai` | `QMap<QString,QString>`, `QList<QStringList>`, `QList<QByteArray>`, `QList<QVariantMap>`, `QMap<QString,QVariantMap>`, `QList<QList<int>>` |
| curé | `(si)`, `(ss)`, `(sss)`, `(ii)`, `(ai)`, `(oa{sv})`, `(iss)` | `std::tuple<…>` (`operator<<` public de `qdbusargument.h`) |

Les métatypes curés sont enregistrés **explicitement**, une fois, par `qDBusRegisterMetaType<T>()`
(idempotent, thread-safe), y compris ceux que Qt 6.11 enregistre déjà mais pas Qt 6.4.2
(`QMap<QString,QVariantMap>`). La table ne dépend jamais du registre implicite de Qt.

Tout autre type d'élément (par exemple `a(sssuda{sv})` ou une struct non listée) donne l'erreur
`argument <n> (<sig>): unsupported D-Bus signature '<elem>' (array element)`, renvoyée avant tout
envoi. L'ensemble curé s'étend au cas par cas, avec un test.

**Mapping naturel** (signature inconnue, ou intérieur d'un `v` sans indication) : booléen → `b` ;
entier → `i` s'il tient sur 32 bits, sinon `x` (m12, à documenter dans le CHANGELOG) ; réel → `d` ;
chaîne → `s` ; tableau → `av` ; objet → `a{sv}`.

**Typage explicite d'un variant.** Aux positions `v` (argument `v`, valeur d'une `a{sv}`), l'objet
`{"@dbus": "<signature>", "value": <json>}` force la signature interne. Exemple :
`{"@dbus": "y", "value": 2}` pour l'`urgency` d'une notification. Ailleurs, `@dbus` n'a pas de sens
spécial.

**`Properties.Set`.** Pour `org.freedesktop.DBus.Properties.Set(s,s,v)`, le type interne du `v` est lu
dans la déclaration `<property name=… type=…>` de l'interface cible (même introspection). La valeur
`0.5` pour `Volume` part donc en `ssv` contenant un `d`.

**`h` (m2).** En sortie, marqueur `"<unix fd: not transferable over MCP>"`. En entrée, erreur
explicite.

**Tests** (`test_coercion`, partie A2) : la matrice prototypée (48 cas, identique en 6.4.2 et 6.11) :
`au`, `a{ss}`, `(si)`, `ay`, `o`, `o` invalide, `g`, `v` (naturel, `@dbus`, objet), `(o,x)`, `as`,
`a{sv}`, `a(si)` (vide et non vide), `a(ai)`, `aas`, `aay`, `aai`, `aa{sv}`, `a{sa{sv}}`,
`a{sa{ss}}`, `asaiu`, `(a(si)v)`, `Properties.Set` en `ssv`, erreur `unsupported` pour une struct hors
table. Plus un **test de robustesse** : fuzz borné (graine fixe) de signatures imbriquées × valeurs
JSON hétérogènes ; critère : le bridge ne meurt jamais, chaque appel rend un succès ou un `isError`.

## 5. Sous-projet C — garde-fous

**Principe.** L'autorisation réelle reste hors du bridge (README, « Security & trust model »). Le
bridge ajoute une **denylist best-effort** des actions destructrices connues et quelques interrupteurs.
Ce n'est **pas une frontière de sécurité**, et le README le dit.

**Options CLI.**

| Option | Effet |
|---|---|
| `--allow-system-bus` | Sans elle, `bus: "system"` est refusé par les trois outils D-Bus (`isError`, message explicite). Le schéma JSON ne change pas (`--emit-skill` reste déterministe). |
| `--deny SERVICE:INTERFACE.METHOD` | Répétable ; ajoute une règle de refus. |
| `--allow SERVICE:INTERFACE.METHOD` | Répétable ; lève une entrée de la denylist intégrée. Ne change rien d'autre. |
| `--default-deny` | Mode allowlist : seuls les appels qui correspondent à un `--allow` passent. |
| `--allow-unique-names` | Autorise les destinations `:N.M`, refusées par défaut (sinon la denylist se contourne avec le nom unique du service, visible dans `dbus_list_services`). |

Motifs : `SERVICE:INTERFACE.METHOD`, le dernier `.` sépare la méthode ; `*` joker par segment ;
comparaison **sensible à la casse** ; le chemin d'objet n'est pas filtré.

**Évaluation** (dans `DBusCallTool::call`, avant l'appel) :
1. destination `:N.M` sans `--allow-unique-names` → refus ;
2. interface = celle fournie, sinon celle du résolveur (§4.2) ; si l'état est « ambigu » ou
   « impossible », la règle est testée sur `SERVICE:*.METHOD` (la plus stricte) : une règle de refus correspond quelle que soit l'interface, une règle `--allow` seulement si son interface est `*`. Si PR5 est mergée
   avant PR3a, le résolveur n'existe pas encore : une interface non fournie est alors traitée comme
   « impossible » ; PR3a branche ensuite le résolveur, sans changer les règles ;
3. `--deny` correspond → refus ; 4. `--allow` correspond → accord ; 5. denylist intégrée correspond →
   refus ; 6. `--default-deny` → refus ; sinon accord.

**Denylist intégrée** (noms vérifiés sur Plasma 6.7 ; revérifiés par introspection lors de la
revalidation) :

- `org.freedesktop.login1:org.freedesktop.login1.Manager.{PowerOff*,Reboot*,Halt*,Suspend*,Hibernate*,HybridSleep*,Sleep*,KExec*,Terminate*,KillSession,KillUser,ScheduleShutdown,SetWallMessage}` (préfixes : variantes `*WithFlags` et `Sleep` — amendement PR5)
- `org.kde.KWin:org.kde.kwin.Scripting.*`
- `org.kde.ksmserver:org.kde.KSMServerInterface.{closeSession,logout*}`
- `org.kde.Shutdown:org.kde.Shutdown.*`
- `org.kde.plasmashell:org.kde.PlasmaShell.evaluateScript`
- `org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.{StartUnit*,StartTransientUnit,RestartUnit,ReloadOrRestartUnit,EnqueueUnitJob,KillUnit*,SetEnvironment,UnsetAndSetEnvironment,PowerOff,Reboot,SoftReboot,Halt,KExec,Exit,SwitchRoot}` et `org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.{Start,Restart,ReloadOrRestart,Kill,EnqueueJob}` (exécution de commandes via `systemd --user` ; `StartUnitWithFlags`, `StartUnitReplace`, `EnqueueUnitJob`, `KillUnitSubgroup`, `SoftReboot`, `SwitchRoot` et l'interface `Unit` — amendement PR5)
- `org.freedesktop.DBus:org.freedesktop.DBus.UpdateActivationEnvironment`

**Emplacement.** Unité `src/core/callpolicy.{h,cpp}` (non installée), construite dans `main.cpp` et
passée au constructeur de `DBusBackend`, puis de `DBusCallTool`. Elle ne passe pas par
`BridgeContext` (ABI). Elle s'applique à l'outil `dbus_call`, pas à `DBusBridge` : les appels internes
du core et ceux des plugins n'y passent pas, et le README le précise.

**Audit.** Une ligne stderr par `dbus_call` :
`plasma-mcp-bridge: audit: <allow|deny> <bus> <service> <path> <interface>.<method> [rule]`.

Les noms (`service`, `path`, `interface`, `method`) ne contenant pas que des caractères admis par D-Bus sont refusés avant la policy (amendement PR5 : une ligne d'audit ne peut pas être forgée). `bus: null` vaut `bus` absent.

**m5.** La validation de `bus` se fait dans les outils (`session` ou `system`, sinon erreur
explicite), pas dans `DBusBridge::connection()`.

**m6.** Suppression de `data/org.kde.plasma.mcpbridge.service` et de son installation. Le nom
`org.kde.plasma.mcpbridge` reste réclamé au démarrage ; le README dit qu'une seule instance le détient
et qu'aucun objet n'est exporté.

**m7.** Un `--plugin` qui ne se charge pas fait sortir le bridge avec le code **2**, en mode serveur
comme avec `--emit-skill`.

**Tests** (`test_policy`), sur bus privé :
- une fixture prend le nom `org.freedesktop.login1` et implémente un faux `PowerOff` qui trace chaque
  appel ; on vérifie le refus et **l'absence de trace**, y compris via le nom unique de la fixture,
  avec et sans `interface` ;
- même vérification pour une fausse `org.freedesktop.systemd1` ;
- bus système refusé sans le flag (et garde-fou : `org.freedesktop.login1` n'apparaît pas sur le
  « bus système » du harnais) ;
- ordre deny > allow > denylist, `--default-deny`, `--allow-unique-names` ;
- format des lignes d'audit, `bus: "sytem"` en erreur, plugin introuvable → exit 2 ;
- plus de `.service` dans le manifeste d'install.

## 6. Sous-projet D — serveur concurrent

**Contrainte.** `Tool::call()` est synchrone et fait partie de l'ABI installée ; il n'est pas modifié.

**Threads.**
- Le thread principal lit stdin, dispatche et écrit stdout. Il traite immédiatement `initialize`,
  `ping`, `tools/list`, `notifications/*`, toute méthode inconnue (`-32601`, par exemple la sonde
  `server/discover` d'un client dual-era tant que F n'est pas livré) et les erreurs de protocole.
- Chaque `tools/call` part dans un `QThreadPool` de **4 workers** (outils intégrés) ou dans une
  **file sérialisée** à un worker (outils de plugins). Le résultat revient au thread principal par
  `QMetaObject::invokeMethod(…, Qt::QueuedConnection)`. Seul `StdioTransport` écrit sur stdout.
- Les réponses peuvent arriver dans le désordre (corrélées par `id`, ce que JSON-RPC autorise).
- Outils de plugins : marqués à l'enregistrement par backend d'origine (`registerAll` compare le
  registre avant et après chaque backend). `ToolRegistry::add()` refuse un nom déjà présent (m13),
  avec un warning ; c'est un changement du corps de la fonction, sans changement de structure.

**Fondement.** QtDBus ne documente pas `QDBusConnection` comme `\threadsafe`, mais ses appels
bloquants sont protégés par les verrous internes de `QDBusConnectionPrivate`. Vérifié en 6.4.2 et
6.11.2 : 8 appels de 1 s sur 4 workers en 2,01 s ; un appel bloquant du thread principal pendant ce
temps répond en 0 ms ; aucun warning sous `QT_FATAL_WARNINGS=1`. Un test de stress l'exerce en CI.

**Timeouts.** `--call-timeout-ms` (défaut 25000) et un argument optionnel `timeout_ms` de `dbus_call`
(change le schéma de l'outil). Implémentation : surcharge **non virtuelle et sans paramètre par
défaut** `DBusBridge::callMethod(…, int timeoutMs)` ; l'ancienne signature délègue avec le défaut.
Le timeout borne un appel D-Bus, pas un outil complet.

**Annulation.** `notifications/cancelled` marque l'id annulé s'il est en vol : la tâche encore en file
est sautée ; une tâche en cours va au bout (un appel D-Bus bloquant n'est pas interruptible) et son
résultat est jeté. Aucune réponse n'est envoyée pour cet id. Id inconnu ou déjà terminé : ignoré, ligne
stderr. Annulation visant l'`initialize` : ignorée. Un outil de plugin annulé occupe sa file jusqu'à
sa fin ou à son timeout.

**Id dupliqué en vol.** La seconde requête est ignorée (ligne stderr) ; exactement une réponse par id.

**Limitation de débit** (MUST MCP « rate limit tool invocations ») : assurée par la borne de
concurrence (4 + 1) et le timeout par appel. La limitation par client relève de la couche de policy
externe. Pas de plafond de file dans ce lot.

**Erreurs de protocole (m3).**
- JSON invalide → `-32700`, `id: null` ; frame qui n'est pas un objet (batch compris) → `-32600`,
  `id: null` ; requête sans `method` ou avec `id: null` → `-32600`.
- Frame contenant `result` ou `error` (réponse parasite) → jamais de réponse, ligne stderr.
- `jsonrpc` absent : toléré (comportement actuel, documenté).
- `tools/call` sans `name` ou avec `arguments` non objet → `-32602`. Les erreurs de coercition et de
  plage restent des résultats `isError`.

**Versions (m8, partie D).** Supportées : `2024-11-05`, `2025-06-18`, `2025-11-25`. Négociation :
écho si la version demandée est supportée ; sinon la plus haute supportée ≤ demandée (comparaison des
dates) ; sinon la plus récente supportée. Jamais d'erreur. `2025-03-26` est volontairement exclue :
elle exige de recevoir des batchs JSON-RPC (« Implementations MUST support receiving JSON-RPC
batches »), retirés en 2025-06-18. Choisir « la plus haute ≤ demandée » plutôt que « la plus
récente » est un écart délibéré au SHOULD, pour qu'un client plafonné à 2025-03-26 reçoive 2024-11-05.

**SIGPIPE (m11).** `signal(SIGPIPE, SIG_IGN)` au démarrage ; `StdioTransport::send()` vérifie le
retour de `fwrite`/`fflush` et passe en arrêt sur erreur.

**Arrêt.** Sur EOF de stdin : plus aucun `tools/call` n'est accepté ; les résultats qui arrivent
pendant 2 s sont encore écrits (drain via `QEventLoop` + `QTimer`, sortie anticipée quand plus aucun
worker n'est actif) ; à l'échéance, `fflush(stdout)`, `fflush(stderr)`, `std::_Exit(0)`. Avec un worker
encore actif, on ne repasse jamais par les destructeurs de `QThreadPool`, `ToolRegistry` ou
`QCoreApplication` : mesuré, `~QThreadPool` attend la fin de l'appel D-Bus (10 s pour un appel de 10 s,
jusqu'à 25 s), et un retour de `main()` détruirait le registre sous un worker. Le pool est alloué sur
le tas et n'est pas détruit dans ce cas ; on n'utilise pas `QThreadPool::globalInstance()`.

**Tests** (`test_async`, `test_protocol`) :
- `ping` et `server/discover` pendant `Slow(4)` → réponse en < 0,5 s ;
- deux `Slow(2)` en parallèle → ≈ 2 s (après auto-test de la fixture : deux `Slow(1)` via python-dbus
  ≈ 1 s) ; stress N × M appels sous `QT_FATAL_WARNINGS=1`, chaque ligne stdout est un JSON valide ;
- annulation : aucune réponse pour l'id annulé, appel suivant normal ; tâche en file sautée ;
- id dupliqué : un seul message pour cet id ;
- `timeout_ms: 1000` sur `SlowBlocking(3)` → `org.freedesktop.DBus.Error.NoReply` vers 1 s ;
  `--call-timeout-ms` idem ;
- deux outils d'un plugin de test minimal (construit dans `tests/`) ne se chevauchent pas ; doublon de
  nom refusé ;
- arrêt : EOF pendant `SlowBlocking(10)` → sortie en < 3 s, code 0 ;
- client qui ferme stdout pendant `Slow(1)` → pas de mort par `SIGPIPE` ;
- `-32700`, `-32600` (batch, `id: null`), `-32602`, réponse parasite sans réponse ;
- négociation : `2024-11-05`, `2025-03-26` → `2024-11-05`, `2025-06-18`, `2025-11-25`,
  `2026-07-28` → `2025-11-25` (avant F), `"1.0.0"` et absente → `2025-11-25`.

## 7. Sous-projet F — MCP 2026-07-28 (dual-era)

La révision 2026-07-28 (courante) change le modèle :
- protocole **sans état** : plus de handshake `initialize`/`notifications/initialized` ;
- `server/discover` : obligatoire (« servers MUST implement this RPC ») ;
- chaque requête porte `_meta["io.modelcontextprotocol/protocolVersion"]` et `clientCapabilities` ;
  une requête qui ne les porte pas est rejetée avec `-32602` ;
- tous les résultats portent un champ `resultType` obligatoire ; `tools/list` porte `ttlMs` et
  `cacheScope` ;
- `ping` et `logging/setLevel` sont retirés ;
- nouvelle erreur `UnsupportedProtocolVersionError` (`-32022`), qui nomme les versions supportées ;
- transport stdio, rétro-compatibilité : un client dual-era sonde `server/discover` avant toute autre
  requête, et se rabat sur `initialize` si le serveur répond par une autre erreur ou ne répond pas.

**F0 — vérification préalable (première tâche, bloquante).** La révision est postérieure aux
connaissances de l'auteur de ce document. Avant tout code, établir la liste exhaustive des MUST/SHOULD
d'un serveur stdio exposant des outils, ligne à ligne contre le texte officiel
(`specification/2026-07-28/…`, changelog, `schema.ts`), et l'ajouter à cette spec. Les points ci-dessus
viennent de la revue et doivent être confirmés.

**Design.**
- **Aiguillage par requête**, sur le thread principal. Une requête est « moderne » si elle porte
  `_meta["io.modelcontextprotocol/protocolVersion"]` ; elle est alors traitée selon 2026-07-28 (et
  rejetée en `-32602` s'il manque `clientCapabilities`). Une requête sans ce champ suit le cycle
  historique (§6). Pas d'état de session global : les deux modes peuvent coexister sur un même
  processus.
- `server/discover` renvoie un `DiscoverResult` complet (versions supportées, capacités, infos
  serveur, `resultType`). Tant que F n'est pas entièrement livré, il reste en `-32601` (§6) : un
  `DiscoverResult` partiel ou un code `-32020…-32099` ferait croire au client que le serveur est
  moderne.
- Une version moderne non supportée → `-32022` avec la liste des versions supportées.
- `tools/list` et `tools/call` modernes : mêmes outils, résultats enrichis de `resultType` (et de
  `ttlMs`/`cacheScope` pour `tools/list`), sans changer la sortie de `--emit-skill`.
- `ping` reste servi aux requêtes historiques.
- Un `initialize` reçu en mode moderne : comportement à fixer en F0 (le texte recommande de nommer les
  versions supportées dans l'erreur pour un serveur moderne-seul ; le bridge reste dual-era et répond
  donc normalement).

**Tests** (`test_modern`) : `server/discover` ; requête moderne sans `clientCapabilities` → `-32602` ;
version moderne inconnue → `-32022` avec la liste ; `tools/list`/`tools/call` modernes (`resultType`,
`ttlMs`, `cacheScope`) ; coexistence moderne et historique sur un même processus ; sonde dual-era puis
repli `initialize` d'un client historique ; liste issue de F0 couverte cas par cas.

## 8. ABI et compatibilité

- Aucun en-tête installé ne change de structure : pas de membre ajouté à `DBusBridge`, `ToolRegistry`
  ou `BridgeContext`.
- Ajout autorisé : la surcharge non virtuelle `callMethod(…, int timeoutMs)` (nouveau symbole).
- `ToolRegistry::add()` refuse désormais un nom en double (comportement documenté).
- `Tool::call()` peut être appelé depuis un worker. Les outils d'un même plugin sont sérialisés ; un
  plugin n'a donc pas à être réentrant, mais ne doit pas supposer s'exécuter sur le thread principal
  (il peut ouvrir sa propre `QDBusConnection` depuis un worker).
- IID inchangé : `org.kde.plasma.mcpbridge.PluginInterface/1.0`.

## 9. Changements visibles (CHANGELOG cumulatif, note de migration 0.2.0)

Nombres exacts (et `t` > 2^53 en chaîne) ; `null` pour une méthode void ; marqueurs
`<unsupported D-Bus type …>` et `<unix fd: not transferable over MCP>` ; erreurs de plage et
`unsupported` en `isError` ; `ay` accepté en base64 strict ; entiers dans `v` en `i` s'ils tiennent
(m12) ; bus système refusé par défaut ; denylist et options de policy ; destinations `:N.M` refusées ;
suppression du `.service` (packagers) ; exit 2 sur plugin introuvable ; réponses possiblement dans le
désordre ; erreurs `-32700`/`-32600`/`-32602` ; versions MCP négociées ; noms d'outils dupliqués
refusés.

## 10. Livraison (PR du core)

La numérotation est commune avec le plan de livraison d'ensemble : les numéros absents (2, 4, 7, 8)
désignent des étapes hors de ce dépôt (consommateurs du core, revalidation finale).

| PR | Contenu | Dépend de |
|---|---|---|
| 1 | Socle de tests (§3) + A0 (B1, M7) + version `0.2.0-dev` + CHANGELOG | — |
| 3a | A1 : nombres, chemin d'appel, résolveur, plages scalaires, `ay` base64 ; README (exemple `nextDesktop`, phrase « Any reply ») | 1 |
| 3b | A2 : coercer, conteneurs, `h`, fuzz ; README (limites de la coercition, `@dbus`) | 3a |
| 5 | C : garde-fous, m5, m6, m7, audit ; README (sécurité, install, portails) | 1 |
| 6 | D : serveur concurrent, m3, m8 (≤ 2025-11-25), m11, m13, timeouts | 1 ; rebase après 5 |
| 6b | F : MCP 2026-07-28 dual-era ; version `0.2.0` et tag | 6 |

Après PR1, les PR 3a, 5 et 6 se développent en parallèle. Ordre de merge : 5 → 6 (conflits sur
`main.cpp`, `dbustools.cpp` et `server.cpp`), 3a → 3b, puis 6b. Chaque PR met à jour README, CLAUDE.md
et CHANGELOG pour ce qu'elle change.

## 11. Critères d'acceptation

- `ctest` vert en CI (Qt 6.4.2) et en local (Qt 6.11) à chaque merge.
- Tous les écarts du §2 sont corrigés, ou retirés de la documentation pour ceux qui relèvent de la
  roadmap.
- Revalidation en conditions réelles (bureau Plasma, bus dbus-broker) après la dernière PR : la
  campagne initiale est rejouée, chaque ligne passe à ✅ ou « retiré (roadmap) », et la denylist est
  contrôlée par introspection des services réels.

## 12. Risques connus

- Tests sous dbus-daemon, production sous dbus-broker : la revalidation finale tourne sous
  dbus-broker, et les tests n'assertent que sur des noms d'erreur.
- Thread-safety de QtDBus fondée sur l'implémentation (§6) : couverte par un test de stress.
- MCP 2026-07-28 : périmètre exact fixé par F0.
