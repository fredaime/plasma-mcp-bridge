// SPDX-License-Identifier: MIT
#pragma once

#include <QtPlugin>

#include <memory>
#include <vector>

class Backend;

// Stable plugin ABI. Out-of-tree plugins implement this interface, expose it
// with Q_PLUGIN_METADATA(IID PLASMA_MCP_BRIDGE_PLUGIN_IID), and the bridge
// loads them via QPluginLoader when given --plugin <path>.
//
// Versioning: the trailing "/<major>.<minor>" in the IID is the ABI version.
// Bump the major when the interface changes incompatibly; QPluginLoader will
// then refuse to load plugins built against the old IID instead of crashing.
class PluginInterface
{
public:
    virtual ~PluginInterface() = default;

    // Each plugin may contribute one or more backends. Ownership transfers to
    // the bridge.
    virtual std::vector<std::unique_ptr<Backend>> createBackends() = 0;
};

#define PLASMA_MCP_BRIDGE_PLUGIN_IID "org.kde.plasma.mcpbridge.PluginInterface/1.0"

Q_DECLARE_INTERFACE(PluginInterface, PLASMA_MCP_BRIDGE_PLUGIN_IID)
