// SPDX-License-Identifier: MIT
#include "tools/dbustools.h"

#include "core/callpolicy.h"
#include "dbus/busconnection.h"
#include "dbus/dbusbridge.h"
#include "dbus/interfaceresolver.h"

#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <limits>

#include <cctype>
#include <cstdio>
#include <cstring>

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
    case QJsonValue::Double: {
        // Format a scalar exactly like a number nested in an array or object:
        // integers in full, reals as the shortest round-trip representation.
        const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
        return QString::fromUtf8(json.mid(1, json.size() - 2));
    }
    case QJsonValue::Null:
        return QStringLiteral("null");
    default:
        return QString();
    }
}

// m5: 'bus' is "session" (also when absent or null) or "system"; anything
// else is an error, not a silent fallback to the session bus. Returns the
// error text, empty on success.
QString busArgument(const QJsonObject &arguments, QString *bus)
{
    const QJsonValue value = arguments.value(QStringLiteral("bus"));
    if (value.isUndefined() || value.isNull()) {
        *bus = QStringLiteral("session");
        return QString();
    }
    const QString name = value.toString();
    if (name != QLatin1String("session") && name != QLatin1String("system"))
        return QStringLiteral("'bus' must be \"session\" or \"system\"");
    *bus = name;
    return QString();
}

// busArgument, then the --allow-system-bus switch.
QString selectBus(const QJsonObject &arguments, const CallPolicy *policy, QString *bus)
{
    const QString error = busArgument(arguments, bus);
    if (!error.isEmpty())
        return error;
    if (*bus == QLatin1String("system") && !policy->systemBusAllowed())
        return CallPolicy::refusal(PolicyDecision{false, QStringLiteral("system-bus")});
    return QString();
}

// Letters, digits, '_' and the characters of `extra`: what D-Bus allows in
// bus names (".-:"), object paths ("/"), interfaces (".") and members ("").
// Checked before the policy and the audit log see a call, so a name can
// neither forge an audit line nor dodge a rule with odd characters.
bool nameCharactersOnly(const QString &text, const char *extra)
{
    for (const QChar c : text) {
        if (c.unicode() >= 128)
            return false;
        const char ch = char(c.unicode());
        if (ch == '\0')
            return false;
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && !std::strchr(extra, ch))
            return false;
    }
    return true;
}

// Straight to stderr, not through Qt's logging: QT_LOGGING_RULES must not be
// able to silence the audit.
void writeAudit(const QString &line)
{
    const QByteArray bytes = line.toUtf8() + '\n';
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
    std::fflush(stderr);
}

QJsonObject busProperty()
{
    QJsonObject bus;
    bus.insert(QStringLiteral("type"), QStringLiteral("string"));
    bus.insert(QStringLiteral("enum"), QJsonArray{QStringLiteral("session"), QStringLiteral("system")});
    bus.insert(QStringLiteral("default"), QStringLiteral("session"));
    bus.insert(QStringLiteral("description"),
               QStringLiteral("Which bus to use: \"session\" (the default; Plasma lives there) "
                              "or \"system\" (refused unless the bridge was started with "
                              "--allow-system-bus)."));
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
    QString bus;
    const QString busError = selectBus(arguments, m_policy, &bus);
    if (!busError.isEmpty())
        return ToolResult::failure(busError);
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
    QString bus;
    const QString busError = selectBus(arguments, m_policy, &bus);
    if (!busError.isEmpty())
        return ToolResult::failure(busError);
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
        "Invoke a method on any D-Bus object and return its reply as JSON. This is the universal "
        "bridge to desktop automation: KWin, plasmashell, global shortcuts, power management, "
        "media players (MPRIS) and any other service on the bus. Arguments are passed "
        "positionally as a JSON array and converted to the types the method declares in its "
        "introspection data: integers are range-checked, a byte array (ay) may be given as a "
        "base64 string, a uint64 beyond the int64 range as a decimal string. A method without "
        "return value replies null. The bridge refuses known destructive methods (power off, "
        "logout, script execution, systemd units), the system bus and unique connection names "
        "(:N.M) unless it was started with the matching --allow option.");
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
            "Interface declaring the method, e.g. org.kde.KWin. May be omitted when exactly one "
            "interface of the object declares the method; give it to choose between several.")));
    properties.insert(QStringLiteral("method"),
                      stringProperty(QStringLiteral("Method to call, e.g. nextDesktop.")));
    QJsonObject timeout;
    timeout.insert(QStringLiteral("type"), QStringLiteral("integer"));
    timeout.insert(QStringLiteral("minimum"), 1);
    timeout.insert(QStringLiteral("description"),
                   QStringLiteral("Milliseconds to wait for each D-Bus round-trip of this call "
                                  "(introspection, then the call). Defaults to the bridge's "
                                  "--call-timeout-ms (25000)."));
    properties.insert(QStringLiteral("timeout_ms"), timeout);
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
    QString bus;
    const QString busError = busArgument(arguments, &bus);
    if (!busError.isEmpty())
        return ToolResult::failure(busError);
    int timeoutMs = m_defaultTimeoutMs;
    const QJsonValue timeoutValue = arguments.value(QStringLiteral("timeout_ms"));
    if (!timeoutValue.isUndefined() && !timeoutValue.isNull()) {
        const double ms = timeoutValue.toDouble();
        if (!timeoutValue.isDouble() || ms < 1 || ms > std::numeric_limits<int>::max()
            || std::floor(ms) != ms)
            return ToolResult::failure(
                QStringLiteral("'timeout_ms' must be an integer between 1 and 2147483647"));
        timeoutMs = static_cast<int>(ms);
    }
    const QString service = arguments.value(QStringLiteral("service")).toString();
    const QString path = arguments.value(QStringLiteral("path")).toString();
    const QString interface = arguments.value(QStringLiteral("interface")).toString();
    const QString method = arguments.value(QStringLiteral("method")).toString();
    const QJsonArray args = arguments.value(QStringLiteral("args")).toArray();

    if (service.isEmpty() || path.isEmpty() || method.isEmpty())
        return ToolResult::failure(QStringLiteral("'service', 'path' and 'method' are required"));

    if (!nameCharactersOnly(service, ".-:") || !nameCharactersOnly(path, "/")
        || !nameCharactersOnly(interface, ".") || !nameCharactersOnly(method, ""))
        return ToolResult::failure(QStringLiteral(
            "'service', 'path', 'interface' and 'method' may only contain the characters D-Bus "
            "allows in names"));

    // The policy judges the interface the call will carry. Without one, the
    // introspection data may name it; the call is then sent with that
    // interface explicitly, so what is checked is what is sent.
    CallTarget target{bus, service, path, interface, method};
    if (target.interface.isEmpty() && m_policy->destinationAllowed(bus, service)) {
        const MethodResolution resolution =
            resolveMethod(busConnection(bus), service, path, QString(), method, timeoutMs);
        if (resolution.timedOut())
            return ToolResult::failure(
                QStringLiteral("%1: %2").arg(resolution.errorName, resolution.errorMessage));
        if (resolution.state == MethodResolution::Unique)
            target.interface = resolution.interface;
    }
    const QDBusConnection connection = busConnection(bus);
    const auto ownerOf = [connection](const QString &name) {
        const QDBusReply<QString> reply = connection.interface()->serviceOwner(name);
        return reply.isValid() ? reply.value() : QString();
    };
    const PolicyDecision decision = m_policy->evaluate(target, ownerOf);
    writeAudit(CallPolicy::auditLine(target, decision));
    if (!decision.allowed)
        return ToolResult::failure(CallPolicy::refusal(decision));

    const DBusResult result =
        m_bridge->callMethod(bus, service, path, target.interface, method, args, timeoutMs);
    if (!result.ok)
        return ToolResult::failure(result.error);
    return ToolResult::ok(stringify(result.value));
}
