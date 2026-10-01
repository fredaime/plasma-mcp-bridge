# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

An MCP (Model Context Protocol) server that lets AI agents automate a KDE Plasma 6
desktop. It speaks MCP over stdio and bridges every operation to **D-Bus**. The
design bet (see README "Why D-Bus") is that the whole Plasma/KDE and freedesktop
automation surface is already published on the session bus, so the server is a
generic D-Bus client rather than something linked against KF6.

Consequence for development: **the only hard dependency is Qt 6 (Core + DBus).**
Do not introduce a hard KF6/Plasma library dependency without a deliberate reason —
prefer reaching Plasma services over D-Bus. The core is desktop-agnostic; only the
service/interface *names* are Plasma-specific.

## Build, run, test

```sh
cmake -S . -B build -G Ninja      # configure (needs extra-cmake-modules + Qt6 Core/DBus)
cmake --build build               # build -> build/bin/plasma-mcp-bridge
DESTDIR=/tmp/stage cmake --install build   # stage install to inspect output
```

Tests: `ctest --test-dir build --output-on-failure`. Each module in `tests/`
is a Python `unittest` run by `tests/run_with_bus.sh` on a private session bus
(no activatable services; the system bus is redirected onto it). Fixtures live
in `tests/fixtures/` (`echo_service.py` is the D-Bus oracle: `Echo*` methods
return the wire signature they received, `Ret*` methods return typed values;
`trap_services.py` stands in for the services of the built-in denylist (login1,
systemd1, KWin, …) on the private bus and records every call it receives, so a
policy test asserts that a refused call never arrived).
`tests/plugin/testplugin.cpp` is a minimal plugin (sleeping tools, a clashing
tool name) built with the tests; `test_async` loads it through
`PLASMA_MCP_TEST_PLUGIN`.
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

Without a live session bus (e.g. in CI/containers), `initialize` and `tools/list`
succeed while D-Bus tool calls return an MCP error result (`isError: true`) — that
is expected, not a regression.

## Architecture

Layers under `src/` (include paths are rooted at `src/`, so headers are included as
`"mcp/..."`, `"dbus/..."`, `"core/..."`, `"backends/..."`, `"tools/..."`):

- **`src/mcp/` — protocol layer (transport-agnostic of D-Bus).**
  - `StdioTransport` reads newline-delimited JSON-RPC from stdin via a
    `QSocketNotifier` and writes responses to stdout. **Nothing else may write to
    stdout** — it would corrupt the protocol stream; use `qInfo/qWarning` (stderr).
  - `Server` (main thread) validates frames (`-32700`/`-32600`/`-32602`),
    negotiates the protocol version (`mcp/protocolversion.*`), answers
    `initialize`, `ping`, `tools/list` and notifications at once, and hands
    each `tools/call` to `ToolRunner` (`mcp/toolrunner.*`): built-in tools on
    a `QThreadPool` of 4, plugin tools on a pool of 1 (threads never retire,
    so a plugin always finds the same thread), results posted back to the main
    thread. Replies may come out of order. On EOF it drains for 2 s,
    then `std::_Exit(0)` if a call still runs (never destroy a pool with a
    running task).
  - `Tool::call()` runs on a worker thread: a tool must not touch
    main-thread-only objects and must not write stdout. `ToolRegistry::add()`
    refuses a name that is already registered.
  - `Tool` is the abstract interface every tool implements (`name`,
    `description`, `inputSchema` as JSON Schema, `call`). `ToolRegistry` owns them.
  - `jsonrpc` has the JSON-RPC result/error envelope helpers and error codes.

- **`src/dbus/` — the bridge.** `DBusBridge` is the single chokepoint to the
  session/system bus: `listServices`, `introspect`, `callMethod`, plus the
  `jsonToVariant` / `variantToJson` / `demarshall` marshalling helpers. The generic
  demarshaller relies on Qt's `operator>>(const QDBusArgument&, QVariant&)` and the
  const `beginArray/beginMap/beginStructure` overloads to turn arbitrary D-Bus
  replies (arrays, structs, `a{sv}` maps) into JSON. `callMethod` introspects the
  target object on every call (`dbus/interfaceresolver.*`, no cache) to find the
  interface declaring the method and its input signature, converts each argument
  to that signature (`dbus/typecoercer.*`; strict for basic types, `as`, `ay`), and
  sends a plain `QDBusMessage::createMethodCall` with the explicit interface —
  never `QDBusInterface`, which rejects the bus daemon and mangles void replies.
  Both units are internal: their headers are not installed.

- **`src/core/` — the provider seam (plugin ABI).**
  - `Backend` is the unit a plugin contributes: it has a `name`, a `description`,
    and one `registerTools()` call that hands tools to the registry.
  - `PluginInterface` (in `core/plugin.h`) is what out-of-tree shared libraries
    implement. The stable IID is
    `org.kde.plasma.mcpbridge.PluginInterface/1.0` — bump the major when the
    ABI changes incompatibly. `PluginLoader` wraps `QPluginLoader`.
  - `SkillEmitter::render(registry)` walks the registry and produces deterministic
    Markdown for a downstream packager that wants to ship an MCP skill alongside
    the bridge. Exposed via the `--emit-skill` CLI flag.
  - `CallPolicy` (`core/callpolicy.*`, internal, not installed) holds the guard
    rails of the D-Bus tools: system-bus switch, unique-name refusal, built-in
    denylist, `--deny`/`--allow`/`--default-deny`, audit line format. `main.cpp`
    builds it from the command line and hands it to `DBusBackend`, which passes
    it to the three D-Bus tools. It is not part of `BridgeContext` (ABI):
    `DBusBridge` and plugins are not filtered. `DBusCallTool` resolves a missing
    interface before asking the policy and sends the call with the interface
    that was judged.

- **`src/backends/` — built-in backends.** `DBusBackend` registers the three
  generic D-Bus tools; `NotificationBackend` registers the freedesktop notification
  tool. They are the template for new built-in capabilities (input, capture, …).

- **`src/tools/` — concrete `Tool` implementations.** Each tool takes a
  `DBusBridge*` and is added to the registry by some backend. `dbustools.{h,cpp}`
  holds the three generic tools; `notifytool.{h,cpp}` is a high-level convenience
  tool over a freedesktop standard.

**To add a tool inside the bridge:** subclass `Tool`, implement the four methods,
and register it from an existing backend's `registerTools()` (or add a new
backend and register it in `main.cpp`). Express the work as `DBusBridge` calls
rather than new dependencies.

**To add an out-of-tree plugin:** subclass `PluginInterface` in a `QObject`-derived
class with `Q_PLUGIN_METADATA(IID PLASMA_MCP_BRIDGE_PLUGIN_IID)` and
`Q_INTERFACES(PluginInterface)`, return your `Backend`s from `createBackends()`,
build as a `MODULE` library, and run the bridge with `--plugin /path/to/lib.so`.
Consume the ABI with `find_package(PlasmaMcpBridge REQUIRED)` +
`target_link_libraries(... PRIVATE PlasmaMcpBridge::PluginInterface)`.

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
- `--allow-unique-names` — accept `:N.M` destinations (the rules still apply
  to the connection behind them).
- `--call-timeout-ms <ms>` — how long `dbus_call` waits for each D-Bus
  round-trip (default 25000); a call's `timeout_ms` overrides it. Invalid value:
  exit code 2.

## Conventions

- Qt string literals use `QStringLiteral` for keys/identifiers; method/interface
  comparisons use `QLatin1String`.
- The strict `QT_NO_CAST_FROM_ASCII` family is intentionally **not** enabled (the
  JSON-heavy code stays readable). `KDECompilerSettings` is deliberately omitted
  from CMake for the same reason; `KDEInstallDirs` and `KDECMakeSettings` are used.
- Source files carry a one-line `// SPDX-License-Identifier: MIT` header.
- The MCP protocol versions the server speaks live in
  `src/mcp/protocolversion.cpp`; the build stamps the package version via the
  `PLASMA_MCP_BRIDGE_VERSION` compile definition (set in `src/CMakeLists.txt`).
- The code must build with `-Werror` on Qt 6.4 (CI) and on current Qt. Do not
  use `qAsConst`, `_qs` or `Q_FOREACH`; deprecation warnings are pinned to the
  6.4 level in the top-level `CMakeLists.txt`.
