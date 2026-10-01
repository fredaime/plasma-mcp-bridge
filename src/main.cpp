// SPDX-License-Identifier: MIT
#include "backends/dbusbackend.h"
#include "backends/notificationbackend.h"
#include "core/backend.h"
#include "core/callpolicy.h"
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

std::vector<std::unique_ptr<Backend>> builtinBackends(const CallPolicy *policy)
{
    std::vector<std::unique_ptr<Backend>> backends;
    backends.push_back(std::make_unique<DBusBackend>(policy));
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

    QCommandLineOption allowSystemBusOption(QStringLiteral("allow-system-bus"),
        QStringLiteral("Let the D-Bus tools reach the system bus (refused by default)."));
    parser.addOption(allowSystemBusOption);

    QCommandLineOption denyOption(QStringLiteral("deny"),
        QStringLiteral("Refuse dbus_call to SERVICE:INTERFACE.METHOD ('*' matches any run of "
                       "characters). May be repeated."),
        QStringLiteral("pattern"));
    parser.addOption(denyOption);

    QCommandLineOption allowOption(QStringLiteral("allow"),
        QStringLiteral("Permit dbus_call to SERVICE:INTERFACE.METHOD although the built-in "
                       "denylist refuses it; with --default-deny, the only calls permitted. "
                       "May be repeated."),
        QStringLiteral("pattern"));
    parser.addOption(allowOption);

    QCommandLineOption defaultDenyOption(QStringLiteral("default-deny"),
        QStringLiteral("Refuse every dbus_call that no --allow pattern matches."));
    parser.addOption(defaultDenyOption);

    QCommandLineOption allowUniqueNamesOption(QStringLiteral("allow-unique-names"),
        QStringLiteral("Accept unique connection names (:N.M) as dbus_call destination. "
                       "The rules still apply to the connection behind them."));
    parser.addOption(allowUniqueNamesOption);

    parser.process(app);

    CallPolicy::Options policyOptions;
    policyOptions.allowSystemBus = parser.isSet(allowSystemBusOption);
    policyOptions.allowUniqueNames = parser.isSet(allowUniqueNamesOption);
    policyOptions.defaultDeny = parser.isSet(defaultDenyOption);
    policyOptions.deny = parser.values(denyOption);
    policyOptions.allow = parser.values(allowOption);
    CallPolicy policy;
    QString policyError;
    if (!policy.configure(policyOptions, &policyError)) {
        qCritical("plasma-mcp-bridge: %s", qUtf8Printable(policyError));
        return 2;
    }

    DBusBridge bridge;
    BridgeContext context{&bridge, QStringLiteral(PLASMA_MCP_BRIDGE_VERSION)};

    ToolRegistry registry;

    auto allBackends = builtinBackends(&policy);
    PluginLoader loader;
    for (const QString &pluginPath : parser.values(pluginOption)) {
        if (!loader.load(pluginPath, &allBackends)) {
            qCritical("plasma-mcp-bridge: aborting: plugin %s could not be loaded",
                      qUtf8Printable(pluginPath));
            return 2;
        }
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
