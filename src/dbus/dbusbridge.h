// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QString>
#include <QVariant>

class QDBusArgument;
class QDBusConnection;

struct DBusResult {
    bool ok = true;
    QString error;
    QJsonValue value;

    static DBusResult success(const QJsonValue &value) { return {true, QString(), value}; }
    static DBusResult failure(const QString &error) { return {false, error, QJsonValue()}; }
};

// Thin, generic gateway to the session and system buses. Every higher-level
// automation tool is expressed as one or more calls through here, which is
// what makes the bridge desktop-agnostic: the wire protocol is D-Bus, and the
// service/interface names decide whether a call targets a freedesktop standard
// or a Plasma-specific (org.kde.*) service.
class DBusBridge
{
public:
    DBusBridge();

    // Claim a well-known name on the session bus so the bridge is visible to
    // the running desktop. Best-effort: returns false when there is no bus.
    bool registerService(const QString &serviceName);

    DBusResult listServices(const QString &busName);
    DBusResult introspect(const QString &busName, const QString &service, const QString &path);
    DBusResult callMethod(const QString &busName, const QString &service, const QString &path,
                          const QString &interface, const QString &method, const QJsonArray &args);

    // JSON <-> D-Bus marshalling. Exposed so tools can reuse them.
    static QVariant jsonToVariant(const QJsonValue &value);
    static QJsonValue variantToJson(const QVariant &value);
    static QJsonValue demarshall(const QDBusArgument &argument);

private:
    static QDBusConnection connection(const QString &busName);
};
