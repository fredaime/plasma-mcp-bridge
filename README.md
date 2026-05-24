# plasma-mcp-bridge

A [Model Context Protocol](https://modelcontextprotocol.io) (MCP) server that lets
AI agents — Claude, Gemini, GPT, or any MCP-aware model — drive a **KDE Plasma 6**
desktop. It speaks MCP over stdio and bridges every request through **D-Bus**, the
mechanism the entire Plasma/KDE and freedesktop desktop stack already exposes its
automation through.

## Why D-Bus

Plasma's automation surface is published on the session bus: KWin window and effect
scripting (`org.kde.KWin`), the shell (`org.kde.plasmashell`), global shortcuts
(`org.kde.kglobalaccel`), power management, activities, clipboard, and more. The
cross-desktop freedesktop standards live on the same bus: notifications
(`org.freedesktop.Notifications`), portals (`org.freedesktop.portal.*`), logind
(`org.freedesktop.login1`), screensaver, and media players (MPRIS).

Because the bridge is a *generic* D-Bus client, a single trio of tools
(`dbus_list_services`, `dbus_introspect`, `dbus_call`) already reaches all of it.
That also means the core works on any freedesktop-compliant desktop, not just
Plasma — only the service/interface names differ. Higher-level convenience tools
(like `desktop_notify`) are layered on top.

This keeps the **only hard dependency Qt 6 (Core + DBus)** — no KF6 linkage is
required, since Plasma is reached over the wire rather than in-process.

## Tools

| Tool | Purpose | Portability |
| --- | --- | --- |
| `dbus_list_services` | List well-known names on the session/system bus | any desktop |
| `dbus_introspect` | Return introspection XML for an object (interfaces, methods, signals) | any desktop |
| `dbus_call` | Invoke any method on any object — the universal automation bridge | any desktop |
| `desktop_notify` | Show a notification via `org.freedesktop.Notifications` | any desktop |

## Building

Requirements: CMake ≥ 3.16, a C++17 compiler, `extra-cmake-modules`, and Qt 6
(`Core`, `DBus`).

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Install (binary + D-Bus service activation file):

```sh
sudo cmake --install build
```

## Registering with an MCP client

The server is launched by the client over stdio. For Claude Desktop / Claude Code,
add to the MCP servers config:

```json
{
  "mcpServers": {
    "plasma": {
      "command": "plasma-mcp-bridge"
    }
  }
}
```

On startup the bridge also claims the well-known name `org.kde.plasma.mcpbridge`
on the session bus, so it is visible to the running desktop (e.g. in
`qdbus`/D-Spy), and installs a D-Bus service file for activation.

## Protocol notes

- Transport is newline-delimited JSON-RPC 2.0 on stdin/stdout. **stdout carries
  only protocol messages**; all diagnostics go to stderr.
- Implemented methods: `initialize`, `notifications/initialized`, `ping`,
  `tools/list`, `tools/call`.

## Contributing

Contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for the build
setup, conventions, and the sign-off/CLA steps. Development happens against the
protected `main` branch via pull requests, and all participants are expected to
follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

This core is released under the **MIT License** — see [LICENSE](LICENSE).

The project follows an open-core model: the core is free and permissively
licensed, while premium components are offered under a separate commercial
license. Because of this, contributions are accepted under the
[Contributor License Agreement](CLA.md), which lets the maintainers distribute
contributions under both the open-source and commercial licenses.
