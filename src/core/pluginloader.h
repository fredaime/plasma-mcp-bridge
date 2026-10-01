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

    // Load one plugin and append the backends it contributes to *backends.
    // Returns false when the plugin cannot be used (missing file, not a
    // plugin, wrong IID); the reason is logged to stderr.
    bool load(const QString &path, std::vector<std::unique_ptr<Backend>> *backends);

private:
    std::vector<std::unique_ptr<QPluginLoader>> m_loaders;
};
