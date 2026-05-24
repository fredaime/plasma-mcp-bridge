# Contributing

Thanks for your interest in improving plasma-mcp-bridge. This document covers how
to build, the conventions we follow, and the legal steps required before a pull
request can be merged.

## Licensing of contributions (read first)

This project uses a **dual-license / open-core** model: the core is released under
the MIT License, and a separate commercial license is offered for premium
components. To keep that model legally possible, every contributor must agree to
the **Contributor License Agreement** in [`CLA.md`](CLA.md) before their first
contribution is merged. The CLA grants the maintainers the right to distribute
your contribution under both the open-source license **and** other licenses
(including commercial ones).

In practice:

1. On your first pull request, the CLA assistant bot will ask you to sign the CLA
   (a one-click acknowledgement recorded against your GitHub account).
2. Every commit must additionally carry a **Developer Certificate of Origin**
   sign-off (`git commit -s`) — see [`DCO`](DCO). The sign-off line must match the
   author's real name and email.

PRs that are not CLA-signed and DCO-signed-off cannot be merged.

## Development setup

Requirements: CMake ≥ 3.16, a C++17 compiler, `extra-cmake-modules`, and Qt 6
(`Core`, `DBus`). On Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake ninja-build \
    extra-cmake-modules qt6-base-dev qt6-base-dev-tools
```

Build and run the smoke test:

```sh
cmake -S . -B build -G Ninja
cmake --build build

printf '%s\n' \
'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{}}}' \
'{"jsonrpc":"2.0","id":2,"method":"tools/list"}' \
| ./build/bin/plasma-mcp-bridge 2>/dev/null
```

CI builds with `-Wall -Wextra -Werror`; please keep the tree warning-free.

## Conventions

The architecture and house style are documented in [`CLAUDE.md`](CLAUDE.md). The
essentials:

- The only hard dependency is Qt 6 (Core + DBus). Reach Plasma/desktop services
  over D-Bus rather than adding a hard KF6 dependency.
- Every source file starts with `// SPDX-License-Identifier: MIT`.
- Express new automation as `DBusBridge` calls; add tools by subclassing `Tool`
  and registering them in `src/main.cpp`.
- **Nothing may write to stdout except protocol messages** — diagnostics go to
  stderr via the `q*` logging macros.

## Workflow

- `main` is protected. Branch off `main`, and open your pull request against it.
- Keep commits focused; write imperative, concise messages that explain the *why*.
- A pull request needs at least one approving review (see `CODEOWNERS`) and a green
  CI build before it can merge.

## Reporting bugs and proposing features

Use the issue templates. For anything security-sensitive, follow
[`SECURITY.md`](SECURITY.md) instead of opening a public issue.

By contributing, you also agree to abide by our
[Code of Conduct](CODE_OF_CONDUCT.md).
