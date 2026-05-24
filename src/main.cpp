// SPDX-License-Identifier: MIT
#include "backends/dbusbackend.h"
#include "backends/notificationbackend.h"
#include "core/backend.h"
#include "core/pluginloader.h"
#include "core/skillemitter.h"
#include "dbus/dbusbridge.h"
#include "mcp/server.h"
#include "mcp/stdiotransport.h"
#include "mcp/toolregistry.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include <memory>
#include <vector>

#ifndef PLASMA_MCP_BRIDGE_VERSION
#define PLASMA_MCP_BRIDGE_VERSION "0.0.0"
#endif

namespace {

std::vector<std::unique_ptr<Backend>> builtinBackends()
{
    std::vector<std::unique_ptr<Backend>> backends;
    backends.push_back(std::make_unique<DBusBackend>());
    backends.push_back(std::make_unique<NotificationBackend>());
    return backends;
}

void registerAll(const std::vector<std::unique_ptr<Backend>> &backends,
                 ToolRegistry *registry, const BridgeContext &context)
{
    for (const auto &backend : backends) {
        const int before = registry->count();
        backend->registerTools(registry, context);
        qInfo("plasma-mcp-bridge: backend '%s' registered %d tool(s)",
              qUtf8Printable(backend->name()), registry->count() - before);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("plasma-mcp-bridge"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PLASMA_MCP_BRIDGE_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Model Context Protocol server bridging AI agents to the KDE Plasma 6 desktop over D-Bus."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption pluginOption(QStringLiteral("plugin"),
        QStringLiteral("Load an out-of-tree backend plugin (.so). May be repeated."),
        QStringLiteral("path"));
    parser.addOption(pluginOption);

    QCommandLineOption emitSkillOption(QStringLiteral("emit-skill"),
        QStringLiteral("Write the deterministic Markdown tool reference to stdout and exit. "
                       "Used by skill packagers to detect drift between an installed skill and "
                       "the currently-shipping tool surface."));
    parser.addOption(emitSkillOption);

    parser.process(app);

    DBusBridge bridge;
    BridgeContext context{&bridge, QStringLiteral(PLASMA_MCP_BRIDGE_VERSION)};

    ToolRegistry registry;

    auto allBackends = builtinBackends();
    PluginLoader loader;
    for (const QString &pluginPath : parser.values(pluginOption)) {
        auto pluginBackends = loader.load(pluginPath);
        for (auto &backend : pluginBackends)
            allBackends.push_back(std::move(backend));
    }

    registerAll(allBackends, &registry, context);

    if (parser.isSet(emitSkillOption)) {
        QTextStream out(stdout);
        out << SkillEmitter::render(registry);
        return 0;
    }

    if (!bridge.registerService(QStringLiteral("org.kde.plasma.mcpbridge"))) {
        qInfo("plasma-mcp-bridge: could not claim org.kde.plasma.mcpbridge on the session bus; "
              "continuing as an stdio MCP server");
    }

    StdioTransport transport;
    Server server(&transport, &registry);
    QObject::connect(&transport, &StdioTransport::closed, &app, &QCoreApplication::quit);
    transport.start();

    qInfo("plasma-mcp-bridge %s ready on stdio with %d tools", PLASMA_MCP_BRIDGE_VERSION,
          registry.count());
    return app.exec();
}
