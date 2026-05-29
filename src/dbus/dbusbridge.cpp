// SPDX-License-Identifier: MIT
#include "dbus/dbusbridge.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusSignature>
#include <QDBusVariant>
#include <QJsonObject>
#include <QMetaType>

#include <cmath>
#include <limits>

DBusBridge::DBusBridge() = default;

QDBusConnection DBusBridge::connection(const QString &busName)
{
    if (busName == QLatin1String("system"))
        return QDBusConnection::systemBus();
    return QDBusConnection::sessionBus();
}

bool DBusBridge::registerService(const QString &serviceName)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    return bus.registerService(serviceName);
}

DBusResult DBusBridge::listServices(const QString &busName)
{
    QDBusConnection bus = connection(busName);
    if (!bus.isConnected())
        return DBusResult::failure(QStringLiteral("Not connected to the %1 bus").arg(busName));

    QDBusConnectionInterface *iface = bus.interface();
    if (!iface)
        return DBusResult::failure(QStringLiteral("No connection interface available"));

    const QDBusReply<QStringList> reply = iface->registeredServiceNames();
    if (!reply.isValid())
        return DBusResult::failure(reply.error().message());

    QStringList names = reply.value();
    names.sort();
    QJsonArray array;
    for (const QString &name : std::as_const(names))
        array.append(name);
    return DBusResult::success(array);
}

DBusResult DBusBridge::introspect(const QString &busName, const QString &service,
                                  const QString &path)
{
    QDBusConnection bus = connection(busName);
    if (!bus.isConnected())
        return DBusResult::failure(QStringLiteral("Not connected to the %1 bus").arg(busName));

    QDBusInterface iface(service, path.isEmpty() ? QStringLiteral("/") : path,
                         QStringLiteral("org.freedesktop.DBus.Introspectable"), bus);
    const QDBusReply<QString> reply = iface.call(QStringLiteral("Introspect"));
    if (!reply.isValid())
        return DBusResult::failure(reply.error().message());
    return DBusResult::success(reply.value());
}

DBusResult DBusBridge::callMethod(const QString &busName, const QString &service,
                                  const QString &path, const QString &interface,
                                  const QString &method, const QJsonArray &args)
{
    QDBusConnection bus = connection(busName);
    if (!bus.isConnected())
        return DBusResult::failure(QStringLiteral("Not connected to the %1 bus").arg(busName));

    QVariantList variantArgs;
    variantArgs.reserve(args.size());
    for (const QJsonValue &arg : args)
        variantArgs.append(jsonToVariant(arg));

    QDBusMessage reply;
    if (!interface.isEmpty()) {
        // Going through QDBusInterface lets Qt introspect the target and
        // coerce our loosely-typed arguments to the method's real signature
        // (e.g. a JSON integer into the uint32 a method expects).
        QDBusInterface iface(service, path, interface, bus);
        if (!iface.isValid()) {
            const QString message = iface.lastError().message();
            return DBusResult::failure(message.isEmpty()
                ? QStringLiteral("No such interface %1 on %2 %3").arg(interface, service, path)
                : message);
        }
        reply = iface.callWithArgumentList(QDBus::Block, method, variantArgs);
    } else {
        QDBusMessage call = QDBusMessage::createMethodCall(service, path, QString(), method);
        call.setArguments(variantArgs);
        reply = bus.call(call, QDBus::Block);
    }

    if (reply.type() == QDBusMessage::ErrorMessage)
        return DBusResult::failure(
            QStringLiteral("%1: %2").arg(reply.errorName(), reply.errorMessage()));

    const QVariantList out = reply.arguments();
    if (out.isEmpty())
        return DBusResult::success(QJsonValue::Null);
    if (out.size() == 1)
        return DBusResult::success(variantToJson(out.first()));

    QJsonArray array;
    for (const QVariant &value : out)
        array.append(variantToJson(value));
    return DBusResult::success(array);
}

QVariant DBusBridge::jsonToVariant(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool();
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (std::floor(d) == d && std::abs(d) < 9.0e15)
            return QVariant(static_cast<qlonglong>(d));
        return d;
    }
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array: {
        QVariantList list;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &element : array)
            list.append(jsonToVariant(element));
        return list;
    }
    case QJsonValue::Object: {
        QVariantMap map;
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            map.insert(it.key(), jsonToVariant(it.value()));
        return map;
    }
    default:
        return QVariant();
    }
}

QJsonValue DBusBridge::variantToJson(const QVariant &value)
{
    if (value.canConvert<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    if (value.canConvert<QDBusSignature>())
        return value.value<QDBusSignature>().signature();
    if (value.canConvert<QDBusVariant>())
        return variantToJson(value.value<QDBusVariant>().variant());
    if (value.canConvert<QDBusArgument>())
        return demarshall(value.value<QDBusArgument>());

    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Short:
    case QMetaType::SChar:
    case QMetaType::Int:
        return value.toInt();
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QMetaType::UInt:
    case QMetaType::LongLong:
        return static_cast<qint64>(value.toLongLong());
    case QMetaType::ULongLong: {
        // A uint64 above INT64_MAX would wrap to a negative number if cast
        // straight to qint64, so fall back to double for those (lossy, but
        // it preserves sign and magnitude).
        const qulonglong u = value.toULongLong();
        if (u <= static_cast<qulonglong>(std::numeric_limits<qint64>::max()))
            return static_cast<qint64>(u);
        return static_cast<double>(u);
    }
    case QMetaType::Float:
    case QMetaType::Double:
        return value.toDouble();
    case QMetaType::QString:
        return value.toString();
    case QMetaType::QByteArray:
        return QString::fromLatin1(value.toByteArray().toBase64());
    case QMetaType::QStringList: {
        QJsonArray array;
        const QStringList list = value.toStringList();
        for (const QString &item : list)
            array.append(item);
        return array;
    }
    case QMetaType::QVariantList: {
        QJsonArray array;
        const QVariantList list = value.toList();
        for (const QVariant &item : list)
            array.append(variantToJson(item));
        return array;
    }
    case QMetaType::QVariantMap: {
        QJsonObject object;
        const QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            object.insert(it.key(), variantToJson(it.value()));
        return object;
    }
    default:
        if (value.canConvert<QString>())
            return value.toString();
        return QJsonValue(
            QStringLiteral("<unrepresentable:%1>").arg(QLatin1String(value.typeName())));
    }
}

QJsonValue DBusBridge::demarshall(const QDBusArgument &argument)
{
    switch (argument.currentType()) {
    case QDBusArgument::BasicType:
    case QDBusArgument::VariantType: {
        QVariant value;
        argument >> value;
        return variantToJson(value);
    }
    case QDBusArgument::ArrayType: {
        QJsonArray array;
        argument.beginArray();
        while (!argument.atEnd()) {
            QVariant element;
            argument >> element;
            array.append(variantToJson(element));
        }
        argument.endArray();
        return array;
    }
    case QDBusArgument::StructureType: {
        QJsonArray array;
        argument.beginStructure();
        while (!argument.atEnd()) {
            QVariant field;
            argument >> field;
            array.append(variantToJson(field));
        }
        argument.endStructure();
        return array;
    }
    case QDBusArgument::MapType: {
        QJsonObject object;
        argument.beginMap();
        while (!argument.atEnd()) {
            argument.beginMapEntry();
            QVariant key;
            QVariant value;
            argument >> key >> value;
            argument.endMapEntry();
            object.insert(key.toString(), variantToJson(value));
        }
        argument.endMap();
        return object;
    }
    default:
        return QJsonValue();
    }
}
