// SPDX-License-Identifier: MIT
#include "tools/dbustools.h"

#include "dbus/dbusbridge.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

QString stringify(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        return QString::number(value.toDouble());
    case QJsonValue::Null:
        return QStringLiteral("null");
    default:
        return QString();
    }
}

QJsonObject busProperty()
{
    QJsonObject bus;
    bus.insert(QStringLiteral("type"), QStringLiteral("string"));
    bus.insert(QStringLiteral("enum"), QJsonArray{QStringLiteral("session"), QStringLiteral("system")});
    bus.insert(QStringLiteral("default"), QStringLiteral("session"));
    bus.insert(QStringLiteral("description"),
               QStringLiteral("Which bus to use. Plasma lives on the session bus."));
    return bus;
}

QJsonObject stringProperty(const QString &description)
{
    QJsonObject property;
    property.insert(QStringLiteral("type"), QStringLiteral("string"));
    property.insert(QStringLiteral("description"), description);
    return property;
}

} // namespace

QString DBusListServicesTool::name() const
{
    return QStringLiteral("dbus_list_services");
}

QString DBusListServicesTool::description() const
{
    return QStringLiteral(
        "List every service (well-known name) currently registered on the session or system "
        "D-Bus. Use this to discover what is available, e.g. org.kde.KWin, org.kde.plasmashell, "
        "org.freedesktop.Notifications.");
}

QJsonObject DBusListServicesTool::inputSchema() const
{
    QJsonObject properties;
    properties.insert(QStringLiteral("bus"), busProperty());

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    return schema;
}

ToolResult DBusListServicesTool::call(const QJsonObject &arguments)
{
    const QString bus = arguments.value(QStringLiteral("bus")).toString(QStringLiteral("session"));
    const DBusResult result = m_bridge->listServices(bus);
    if (!result.ok)
        return ToolResult::failure(result.error);
    return ToolResult::ok(stringify(result.value));
}

QString DBusIntrospectTool::name() const
{
    return QStringLiteral("dbus_introspect");
}

QString DBusIntrospectTool::description() const
{
    return QStringLiteral(
        "Return the D-Bus introspection XML for an object path, listing its interfaces, methods, "
        "signals and properties. Call this before dbus_call to learn the exact method names and "
        "argument signatures.");
}

QJsonObject DBusIntrospectTool::inputSchema() const
{
    QJsonObject path = stringProperty(
        QStringLiteral("Object path to introspect, e.g. /KWin or / (defaults to /)."));
    path.insert(QStringLiteral("default"), QStringLiteral("/"));

    QJsonObject properties;
    properties.insert(QStringLiteral("bus"), busProperty());
    properties.insert(QStringLiteral("service"),
                      stringProperty(QStringLiteral("Service name, e.g. org.kde.KWin.")));
    properties.insert(QStringLiteral("path"), path);

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("service")});
    return schema;
}

ToolResult DBusIntrospectTool::call(const QJsonObject &arguments)
{
    const QString bus = arguments.value(QStringLiteral("bus")).toString(QStringLiteral("session"));
    const QString service = arguments.value(QStringLiteral("service")).toString();
    const QString path = arguments.value(QStringLiteral("path")).toString(QStringLiteral("/"));
    if (service.isEmpty())
        return ToolResult::failure(QStringLiteral("'service' is required"));

    const DBusResult result = m_bridge->introspect(bus, service, path);
    if (!result.ok)
        return ToolResult::failure(result.error);
    return ToolResult::ok(stringify(result.value));
}

QString DBusCallTool::name() const
{
    return QStringLiteral("dbus_call");
}

QString DBusCallTool::description() const
{
    return QStringLiteral(
        "Invoke a method on any D-Bus object and return its reply. This is the universal bridge to "
        "desktop automation: KWin window/effect scripting, plasmashell, global shortcuts, power "
        "management, media players (MPRIS), portals, and any other service. Arguments are passed "
        "positionally as a JSON array and coerced to the method's signature.");
}

QJsonObject DBusCallTool::inputSchema() const
{
    QJsonObject args;
    args.insert(QStringLiteral("type"), QStringLiteral("array"));
    args.insert(QStringLiteral("description"),
                QStringLiteral("Positional arguments for the method, in order. Defaults to []."));
    args.insert(QStringLiteral("items"), QJsonObject{});

    QJsonObject properties;
    properties.insert(QStringLiteral("bus"), busProperty());
    properties.insert(QStringLiteral("service"),
                      stringProperty(QStringLiteral("Service name, e.g. org.kde.KWin.")));
    properties.insert(QStringLiteral("path"),
                      stringProperty(QStringLiteral("Object path, e.g. /KWin.")));
    properties.insert(
        QStringLiteral("interface"),
        stringProperty(QStringLiteral(
            "Interface name, e.g. org.kde.KWin. Recommended; required for reliable type coercion.")));
    properties.insert(QStringLiteral("method"),
                      stringProperty(QStringLiteral("Method to call, e.g. nextDesktop.")));
    properties.insert(QStringLiteral("args"), args);

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    schema.insert(QStringLiteral("required"),
                  QJsonArray{QStringLiteral("service"), QStringLiteral("path"),
                             QStringLiteral("method")});
    return schema;
}

ToolResult DBusCallTool::call(const QJsonObject &arguments)
{
    const QString bus = arguments.value(QStringLiteral("bus")).toString(QStringLiteral("session"));
    const QString service = arguments.value(QStringLiteral("service")).toString();
    const QString path = arguments.value(QStringLiteral("path")).toString();
    const QString interface = arguments.value(QStringLiteral("interface")).toString();
    const QString method = arguments.value(QStringLiteral("method")).toString();
    const QJsonArray args = arguments.value(QStringLiteral("args")).toArray();

    if (service.isEmpty() || path.isEmpty() || method.isEmpty())
        return ToolResult::failure(QStringLiteral("'service', 'path' and 'method' are required"));

    const DBusResult result = m_bridge->callMethod(bus, service, path, interface, method, args);
    if (!result.ok)
        return ToolResult::failure(result.error);
    return ToolResult::ok(stringify(result.value));
}
