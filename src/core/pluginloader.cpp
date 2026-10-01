// SPDX-License-Identifier: MIT
#include "core/pluginloader.h"

#include "core/backend.h"
#include "core/plugin.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QPluginLoader>

PluginLoader::PluginLoader() = default;
PluginLoader::~PluginLoader() = default;

bool PluginLoader::load(const QString &path, std::vector<std::unique_ptr<Backend>> *backends)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        qWarning("plasma-mcp-bridge: plugin not found: %s", qUtf8Printable(path));
        return false;
    }

    auto loader = std::make_unique<QPluginLoader>(info.absoluteFilePath());
    QObject *root = loader->instance();
    if (!root) {
        qWarning("plasma-mcp-bridge: failed to load plugin %s: %s",
                 qUtf8Printable(path), qUtf8Printable(loader->errorString()));
        return false;
    }

    auto *plugin = qobject_cast<PluginInterface *>(root);
    if (!plugin) {
        qWarning("plasma-mcp-bridge: %s does not implement PluginInterface (IID mismatch?)",
                 qUtf8Printable(path));
        loader->unload();
        return false;
    }

    for (auto &backend : plugin->createBackends())
        backends->push_back(std::move(backend));
    m_loaders.push_back(std::move(loader));
    return true;
}
