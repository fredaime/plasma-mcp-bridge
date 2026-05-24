// SPDX-License-Identifier: MIT
#include "tools/notifytool.h"

#include "dbus/dbusbridge.h"

#include <QJsonArray>

QString NotifyTool::name() const
{
    return QStringLiteral("desktop_notify");
}

QString NotifyTool::description() const
{
    return QStringLiteral(
        "Show a desktop notification through the freedesktop Notifications service. Returns the "
        "notification id assigned by the server.");
}

QJsonObject NotifyTool::inputSchema() const
{
    auto stringProp = [](const QString &description) {
        QJsonObject property;
        property.insert(QStringLiteral("type"), QStringLiteral("string"));
        property.insert(QStringLiteral("description"), description);
        return property;
    };

    QJsonObject appName = stringProp(QStringLiteral("Application name shown by the server."));
    appName.insert(QStringLiteral("default"), QStringLiteral("Plasma MCP Bridge"));

    QJsonObject icon = stringProp(QStringLiteral("Optional icon name, e.g. dialog-information."));
    icon.insert(QStringLiteral("default"), QString());

    QJsonObject timeout;
    timeout.insert(QStringLiteral("type"), QStringLiteral("integer"));
    timeout.insert(QStringLiteral("description"),
                   QStringLiteral("Expiry in milliseconds; -1 lets the server decide, 0 never "
                                  "expires."));
    timeout.insert(QStringLiteral("default"), -1);

    QJsonObject properties;
    properties.insert(QStringLiteral("summary"),
                      stringProp(QStringLiteral("Notification title.")));
    properties.insert(QStringLiteral("body"), stringProp(QStringLiteral("Notification body text.")));
    properties.insert(QStringLiteral("app_name"), appName);
    properties.insert(QStringLiteral("icon"), icon);
    properties.insert(QStringLiteral("timeout"), timeout);

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("summary")});
    return schema;
}

ToolResult NotifyTool::call(const QJsonObject &arguments)
{
    const QString summary = arguments.value(QStringLiteral("summary")).toString();
    if (summary.isEmpty())
        return ToolResult::failure(QStringLiteral("'summary' is required"));

    const QString body = arguments.value(QStringLiteral("body")).toString();
    const QString appName =
        arguments.value(QStringLiteral("app_name")).toString(QStringLiteral("Plasma MCP Bridge"));
    const QString icon = arguments.value(QStringLiteral("icon")).toString();
    const int timeout = arguments.value(QStringLiteral("timeout")).toInt(-1);

    // org.freedesktop.Notifications.Notify(app_name, replaces_id, app_icon,
    //   summary, body, actions, hints, expire_timeout)
    const QJsonArray args{appName,         0,           icon,        summary,
                          body,            QJsonArray{}, QJsonObject{}, timeout};

    const DBusResult result = m_bridge->callMethod(
        QStringLiteral("session"), QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"), args);

    if (!result.ok)
        return ToolResult::failure(result.error);
    return ToolResult::ok(
        QStringLiteral("Notification shown (id %1)").arg(result.value.toInt()));
}
