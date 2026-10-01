// SPDX-License-Identifier: MIT
#include "dbus/interfaceresolver.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QList>
#include <QXmlStreamReader>

MethodResolution resolveMethodFromXml(const QString &xml, const QString &interface,
                                      const QString &method)
{
    QList<MethodResolution> candidates; // one per interface declaring `method`
    MethodResolution current;
    QString currentInterface;
    bool inMethod = false;
    int nodeDepth = 0; // 1 = the introspected object; deeper = child descriptions

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("node")) {
            ++nodeDepth;
        } else if (reader.isEndElement() && reader.name() == QLatin1String("node")) {
            --nodeDepth;
        } else if (nodeDepth > 1) {
            // A child node may carry a full description of its own
            // interfaces; they do not belong to the introspected object.
            continue;
        } else if (reader.isStartElement()) {
            const QXmlStreamAttributes attributes = reader.attributes();
            if (reader.name() == QLatin1String("interface")) {
                currentInterface = attributes.value(QLatin1String("name")).toString();
            } else if (reader.name() == QLatin1String("method")
                       && attributes.value(QLatin1String("name")) == method
                       && (interface.isEmpty() || currentInterface == interface)) {
                inMethod = true;
                current = MethodResolution{};
                current.state = MethodResolution::Unique;
                current.interface = currentInterface;
            } else if (inMethod && reader.name() == QLatin1String("arg")) {
                // Method arguments default to direction "in".
                const auto direction = attributes.value(QLatin1String("direction"));
                if (direction.isEmpty() || direction == QLatin1String("in"))
                    current.inSignature.append(attributes.value(QLatin1String("type")).toString());
            }
        } else if (reader.isEndElement()) {
            if (inMethod && reader.name() == QLatin1String("method")) {
                candidates.append(current);
                inMethod = false;
            } else if (reader.name() == QLatin1String("interface")) {
                currentInterface.clear();
            }
        }
    }

    MethodResolution result;
    result.interface = interface;
    if (reader.hasError() || candidates.isEmpty())
        return result; // Unavailable
    if (candidates.size() > 1) {
        result.state = MethodResolution::Ambiguous;
        return result;
    }
    return candidates.first();
}

MethodResolution resolveMethod(const QDBusConnection &bus, const QString &service,
                               const QString &path, const QString &interface,
                               const QString &method, int timeoutMs)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(
        service, path, QStringLiteral("org.freedesktop.DBus.Introspectable"),
        QStringLiteral("Introspect"));
    const QDBusMessage reply = bus.call(call, QDBus::Block, timeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        MethodResolution result;
        result.interface = interface;
        return result; // Unavailable
    }
    return resolveMethodFromXml(reply.arguments().first().toString(), interface, method);
}
