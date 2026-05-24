// SPDX-License-Identifier: MIT
#include "backends/notificationbackend.h"

#include "mcp/toolregistry.h"
#include "tools/notifytool.h"

QString NotificationBackend::name() const
{
    return QStringLiteral("notifications");
}

QString NotificationBackend::description() const
{
    return QStringLiteral("Show desktop notifications via the freedesktop Notifications service.");
}

void NotificationBackend::registerTools(ToolRegistry *registry, const BridgeContext &context)
{
    registry->add(std::make_unique<NotifyTool>(context.dbus));
}
