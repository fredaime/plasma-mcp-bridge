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

There is no unit-test suite yet. Smoke-test the protocol by piping
newline-delimited JSON-RPC into the binary (stdout is protocol, stderr is logs):

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
  - `Server` dispatches `initialize`, `notifications/initialized`, `ping`,
    `tools/list`, `tools/call`.
  - `Tool` is the abstract interface every tool implements (`name`,
    `description`, `inputSchema` as JSON Schema, `call`). `ToolRegistry` owns them.
  - `jsonrpc` has the JSON-RPC result/error envelope helpers and error codes.

- **`src/dbus/` — the bridge.** `DBusBridge` is the single chokepoint to the
  session/system bus: `listServices`, `introspect`, `callMethod`, plus the
  `jsonToVariant` / `variantToJson` / `demarshall` marshalling helpers. The generic
  demarshaller relies on Qt's `operator>>(const QDBusArgument&, QVariant&)` and the
  const `beginArray/beginMap/beginStructure` overloads to turn arbitrary D-Bus
  replies (arrays, structs, `a{sv}` maps) into JSON. Argument type coercion (e.g. a
  JSON int into a uint32) happens by routing calls through `QDBusInterface` when an
  interface name is supplied.

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

- `--plugin <path>` — load a backend plugin. Repeatable.
- `--emit-skill` — write the deterministic Markdown tool reference (every
  registered tool, including those contributed by `--plugin`) to stdout and exit.
  Used by skill packagers to detect drift between an installed skill and the
  shipping tool surface.

## Conventions

- Qt string literals use `QStringLiteral` for keys/identifiers; method/interface
  comparisons use `QLatin1String`.
- The strict `QT_NO_CAST_FROM_ASCII` family is intentionally **not** enabled (the
  JSON-heavy code stays readable). `KDECompilerSettings` is deliberately omitted
  from CMake for the same reason; `KDEInstallDirs` and `KDECMakeSettings` are used.
- Source files carry a one-line `// SPDX-License-Identifier: MIT` header.
- The MCP protocol version the server advertises lives in `kDefaultProtocolVersion`
  in `src/mcp/server.cpp`; the build stamps the package version via the
  `PLASMA_MCP_BRIDGE_VERSION` compile definition (set in `src/CMakeLists.txt`).
