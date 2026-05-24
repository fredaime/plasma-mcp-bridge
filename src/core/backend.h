// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

class DBusBridge;
class ToolRegistry;

// What the bridge gives a backend when it is asked to register its tools.
// Stays small on purpose: every field here becomes part of the plugin ABI.
struct BridgeContext {
    DBusBridge *dbus = nullptr;
    QString bridgeVersion;
};

// A capability the bridge can offer. Each backend contributes one or more
// MCP tools to the registry. The set of built-in backends is fixed at compile
// time; additional backends arrive at runtime via plugins (see plugin.h).
class Backend
{
public:
    virtual ~Backend() = default;

    // Stable identifier, e.g. "dbus", "notifications", "atspi".
    virtual QString name() const = 0;

    // One-line human-readable description used in diagnostics and the skill.
    virtual QString description() const = 0;

    // Add this backend's tools to the registry. Called exactly once at startup.
    virtual void registerTools(ToolRegistry *registry, const BridgeContext &context) = 0;
};
