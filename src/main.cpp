// SPDX-License-Identifier: MIT
#include "dbus/dbusbridge.h"
#include "mcp/server.h"
#include "mcp/stdiotransport.h"
#include "mcp/toolregistry.h"
#include "tools/dbustools.h"
#include "tools/notifytool.h"

#include <QCommandLineParser>
#include <QCoreApplication>

#include <memory>

#ifndef PLASMA_MCP_BRIDGE_VERSION
#define PLASMA_MCP_BRIDGE_VERSION "0.0.0"
#endif

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
    parser.process(app);

    DBusBridge bridge;
    if (!bridge.registerService(QStringLiteral("org.kde.plasma.mcpbridge"))) {
        qInfo("plasma-mcp-bridge: could not claim org.kde.plasma.mcpbridge on the session bus; "
              "continuing as an stdio MCP server");
    }

    ToolRegistry registry;
    registry.add(std::make_unique<DBusListServicesTool>(&bridge));
    registry.add(std::make_unique<DBusIntrospectTool>(&bridge));
    registry.add(std::make_unique<DBusCallTool>(&bridge));
    registry.add(std::make_unique<NotifyTool>(&bridge));

    StdioTransport transport;
    Server server(&transport, &registry);
    QObject::connect(&transport, &StdioTransport::closed, &app, &QCoreApplication::quit);
    transport.start();

    qInfo("plasma-mcp-bridge %s ready on stdio with %d tools", PLASMA_MCP_BRIDGE_VERSION,
          registry.count());
    return app.exec();
}
