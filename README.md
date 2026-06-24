# plasma-mcp-bridge

[![build](https://github.com/fredaime/plasma-mcp-bridge/actions/workflows/build.yml/badge.svg)](https://github.com/fredaime/plasma-mcp-bridge/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![MCP](https://img.shields.io/badge/Model_Context_Protocol-server-7b3fe4.svg)](https://modelcontextprotocol.io)

**Drive a KDE Plasma 6 desktop from any MCP-aware AI agent.**

`plasma-mcp-bridge` is a [Model Context Protocol](https://modelcontextprotocol.io)
server that exposes the desktop to assistants like Claude, Gemini, or GPT. It
speaks MCP over stdio and turns every request into a **D-Bus** call — the bus that
the whole Plasma/KDE and freedesktop stack already publishes its automation on. So
instead of binding to dozens of libraries, the bridge is one small, generic D-Bus
client that can reach all of it.

## Highlights

- **Universal bridge.** Call any method on any object on the session or system bus
  — KWin scripting, plasmashell, global shortcuts, power management, media players
  (MPRIS), portals, and more.
- **Discovery built in.** Agents can list services and read introspection XML, so
  they figure out what the desktop offers at runtime instead of being hard-coded.
- **Not just Plasma.** The generic tools are desktop-agnostic; freedesktop
  standards (notifications, portals, logind, MPRIS) work on any compliant desktop.
  Only the service names are Plasma-specific.
- **Tiny footprint.** The only hard dependency is **Qt 6 (Core + DBus)** — no KF6
  linkage, because Plasma is reached over the wire.
- **Protocol-clean.** stdout carries only JSON-RPC; all logs go to stderr.

## How it works

```
AI agent  ──MCP (JSON-RPC / stdio)──▶  plasma-mcp-bridge  ──D-Bus──▶  Plasma / freedesktop services
```

Plasma's automation surface lives on the session bus: `org.kde.KWin`,
`org.kde.plasmashell`, `org.kde.kglobalaccel`, `org.kde.ActivityManager`, alongside
cross-desktop standards like `org.freedesktop.Notifications`,
`org.freedesktop.portal.*`, and `org.mpris.MediaPlayer2.*`. Because the bridge is a
generic D-Bus client, a single set of tools reaches all of it.

## Security & trust model

> [!WARNING]
> **Security notice** — The generic `dbus_call` capability exposes a powerful
> local automation surface. This repository is intended for controlled
> experimentation. Production deployments should enforce explicit service and
> method allowlists, scoped identities, confirmation policies, audit logging, and
> revocation **outside the model**.

Treat the model as untrusted. Authorization belongs on the path between what the
model *proposes* and what actually reaches the bus — not inside the model, and not
inside the bridge:

```
Model
  │ proposes
  ▼
MCP client
  │ requests
  ▼
Policy / identity / approval
  │ authorizes
  ▼
plasma-mcp-bridge
  │ invokes
  ▼
D-Bus service
```

The **Policy / identity / approval** stage is not implemented in the public core
yet — but the diagram shows where it belongs. Every request should pass through it
before the bridge invokes D-Bus, so that allow/deny decisions, scoping, and audit
live in a component the model can neither reach nor rewrite.

### Walkthrough

The first three steps run against today's tools; the last two are the policy
layer's responsibility:

1. **Discover** — the agent calls `dbus_list_services` and finds
   `org.freedesktop.Notifications` on the bus.
2. **Introspect** — `dbus_introspect` on `/org/freedesktop/Notifications` returns
   the interface XML, revealing the `Notify` method.
3. **Request** — the agent calls `dbus_call` (or the `desktop_notify` helper) to
   invoke `Notify`.
4. **Authorize** — the policy layer matches the request against its allowlist;
   `Notify` is permitted, so it reaches the bus and the notification appears.
5. **Reject** — later the agent tries a method that is *not* allowlisted — say
   `PowerOff` on `org.freedesktop.login1` — and the policy layer denies it before
   it ever reaches D-Bus.

Step 5 is the whole point: the bridge can reach anything on the bus, so the safety
lives in the layer that decides what it *may* reach.

## Requirements

- CMake ≥ 3.16, a C++17 compiler, Ninja
- `extra-cmake-modules`
- Qt 6 (`Core`, `DBus`)

On Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build \
    extra-cmake-modules qt6-base-dev qt6-base-dev-tools
```

## Build & install

```sh
cmake -S . -B build -G Ninja
cmake --build build           # -> build/bin/plasma-mcp-bridge
sudo cmake --install build    # installs the binary + a D-Bus service file
```

## Connect it to an agent

The server is launched by the MCP client over stdio. Add it to your client's MCP
configuration, e.g.:

```json
{
  "mcpServers": {
    "plasma": {
      "command": "plasma-mcp-bridge"
    }
  }
}
```

At startup the bridge also claims the well-known name `org.kde.plasma.mcpbridge`
on the session bus, so it shows up in tools like `qdbus` and D-Spy.

## Tools

| Tool | What it does |
| --- | --- |
| `dbus_list_services` | List the well-known names on the session/system bus |
| `dbus_introspect` | Return an object's introspection XML (interfaces, methods, signals) |
| `dbus_call` | Invoke any method on any object — the universal automation bridge |
| `desktop_notify` | Show a notification via `org.freedesktop.Notifications` |

## Usage

A typical agent flow is **discover → introspect → call**. Tools are invoked with
the standard MCP `tools/call` envelope; here are the `arguments` payloads.

Show a notification (high-level helper):

```json
{ "summary": "Build finished", "body": "All green ✔", "icon": "dialog-positive" }
```

Discover what KWin exposes, then call it:

```json
// 1. dbus_introspect
{ "service": "org.kde.KWin", "path": "/KWin" }

// 2. dbus_call — switch to the next virtual desktop (names from step 1)
{ "service": "org.kde.KWin", "path": "/KWin",
  "interface": "org.kde.KWin", "method": "nextDesktop", "args": [] }
```

Any reply is marshalled back to JSON, including arrays, structs, and `a{sv}` maps.
Byte arrays (`ay`) come back as base64-encoded strings.

## Protocol

- Transport: newline-delimited JSON-RPC 2.0 over stdin/stdout.
- Implemented: `initialize`, `notifications/initialized`, `ping`, `tools/list`,
  `tools/call`.
- Failed D-Bus calls return an MCP tool error (`isError: true`), not a transport
  error — so an agent can read the message and retry.

## Extending: backends and plugins

The bridge exposes a small **plugin ABI** so additional capabilities (richer
input, screen capture, accessibility, alternative desktops) can ship as
independent shared libraries instead of forks.

A plugin implements `PluginInterface` (in `<core/plugin.h>`)
and returns one or more `Backend` objects; each backend adds its `Tool`s to the
registry at startup. The ABI is `org.kde.plasma.mcpbridge.PluginInterface/1.0`.
Consume it from CMake with:

```cmake
find_package(PlasmaMcpBridge REQUIRED)
add_library(my_backend MODULE my_backend.cpp)
target_link_libraries(my_backend PRIVATE PlasmaMcpBridge::PluginInterface)
```

Then run the bridge with `--plugin /path/to/libmy_backend.so` (repeatable).
See `CLAUDE.md` for the full ABI surface.

`--emit-skill` writes a deterministic Markdown reference of every registered
tool (built-in + plugins) to stdout — useful for packagers that ship a Claude
Code or other MCP-client skill alongside the bridge and want to fail CI when
the skill drifts from the live tool surface.

## Roadmap

- Input control (pointer + keyboard) across X11 and Wayland
- Screen capture
- Semantic UI access via the accessibility bus (AT-SPI)
- Higher-level Plasma tools (windows, activities, shortcuts)
- Additional desktop backends

## Contributing

Contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for build setup,
conventions, and the sign-off/CLA steps, and [CLAUDE.md](CLAUDE.md) for the
architecture in depth.

## License

MIT — see [LICENSE](LICENSE). The project follows an open-core model: this core is
free and permissively licensed, with premium components offered separately under a
commercial license.
