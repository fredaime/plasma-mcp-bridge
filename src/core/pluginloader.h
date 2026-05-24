// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

#include <memory>
#include <vector>

class Backend;
class QPluginLoader;

// Loads out-of-tree plugins (.so / .dylib) that implement PluginInterface and
// hands back the Backend objects they contribute. Each QPluginLoader is kept
// alive for the lifetime of this loader so the underlying library is not
// unloaded while its backends are in use.
class PluginLoader
{
public:
    PluginLoader();
    ~PluginLoader();

    // Load one plugin. Returns the backends it contributed, empty on failure.
    // Failure modes (missing file, wrong IID, etc.) are logged to stderr.
    std::vector<std::unique_ptr<Backend>> load(const QString &path);

private:
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
};
