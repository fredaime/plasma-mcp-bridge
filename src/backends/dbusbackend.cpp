// SPDX-License-Identifier: MIT
#include "backends/dbusbackend.h"

#include "mcp/toolregistry.h"
#include "tools/dbustools.h"

QString DBusBackend::name() const
{
    return QStringLiteral("dbus");
}

QString DBusBackend::description() const
{
    return QStringLiteral(
        "Generic D-Bus access (list services, introspect objects, call methods). "
        "This is how the bridge reaches every Plasma and freedesktop service.");
}

void DBusBackend::registerTools(ToolRegistry *registry, const BridgeContext &context)
{
    registry->add(std::make_unique<DBusListServicesTool>(context.dbus));
    registry->add(std::make_unique<DBusIntrospectTool>(context.dbus));
    registry->add(std::make_unique<DBusCallTool>(context.dbus));
}
