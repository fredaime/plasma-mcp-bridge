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

Three layers, each in its own directory under `src/` (include paths are rooted at
`src/`, so headers are included as `"mcp/..."`, `"dbus/..."`, `"tools/..."`):

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

- **`src/tools/` — concrete tools.** Each tool takes a `DBusBridge*` and is
  registered in `main.cpp`. `dbustools.{h,cpp}` holds the three generic tools;
  `notifytool.{h,cpp}` is a high-level convenience tool over a freedesktop standard
  and serves as the template for richer Plasma-specific tools.

**To add a tool:** subclass `Tool`, implement the four methods (return a JSON Schema
from `inputSchema`), and `registry.add(std::make_unique<YourTool>(&bridge))` in
`main.cpp`. Express the work as `DBusBridge` calls rather than new dependencies.

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
