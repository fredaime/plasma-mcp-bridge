// SPDX-License-Identifier: MIT
#include "dbus/dbusbridge.h"

#include "dbus/busconnection.h"
#include "dbus/interfaceresolver.h"
#include "dbus/typecoercer.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusSignature>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QJsonObject>
#include <QMetaType>

#include <cmath>
#include <limits>

DBusBridge::DBusBridge() = default;

QDBusConnection DBusBridge::connection(const QString &busName)
{
    return busConnection(busName);
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

    const MethodResolution resolution = resolveMethod(bus, service, path, interface, method);

    // Typed conversion when the introspection data names exactly one method
    // with this argument count; otherwise the loose mapping (the remote then
    // rejects a mismatch itself).
    const bool typed = resolution.state == MethodResolution::Unique
        && resolution.inSignature.size() == args.size();
    QVariantList variantArgs;
    variantArgs.reserve(args.size());
    for (int i = 0; i < args.size(); ++i) {
        if (!typed) {
            variantArgs.append(jsonToVariant(args.at(i)));
            continue;
        }
        QVariant value;
        QString error;
        if (!TypeCoercer::coerce(args.at(i), resolution.inSignature.at(i), &value, &error))
            return DBusResult::failure(QStringLiteral("argument %1 (%2): %3")
                                           .arg(i).arg(resolution.inSignature.at(i), error));
        variantArgs.append(value);
    }

    // A plain method call with an explicit interface. QDBusInterface is not
    // used: its isValid() relies on name-owner tracking, which the bus daemon
    // does not have ("No such interface org.freedesktop.DBus"), and it turns
    // a void reply into an invalid QVariant.
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, resolution.interface, method);
    call.setArguments(variantArgs);
    const QDBusMessage reply = bus.call(call, QDBus::Block);

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
        // Beyond 2^53 a JSON number no longer holds the value exactly (clients
        // parse numbers as doubles): emit the exact decimal string instead.
        const qulonglong u = value.toULongLong();
        if (u <= (Q_UINT64_C(1) << 53))
            return static_cast<qint64>(u);
        return QString::number(u);
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

// Extract the current basic-typed element into a properly typed lvalue.
// Extracting into a QVariant (operator>>(..., QVariant&)) is NOT equivalent:
// that overload assumes the current element is a D-Bus variant ('v') and
// recurses into it unconditionally — on any other element type the libdbus
// iterator recursion crashes. So dispatch on the wire signature instead.
//
// Returns an invalid QVariant for a wire type we do not know how to read. In
// that case NOTHING was consumed: every QDBusArgument::operator>> is
// type-checked and, on a mismatch, returns a default value without advancing
// the iterator. A blind read here would leave the caller's atEnd() loop
// spinning forever.
static QVariant extractBasic(const QDBusArgument &argument)
{
    const QString signature = argument.currentSignature();
    switch (signature.isEmpty() ? '\0' : signature.at(0).toLatin1()) {
    case 'b': { bool v; argument >> v; return v; }
    case 'y': { uchar v; argument >> v; return v; }
    case 'n': { short v; argument >> v; return v; }
    case 'q': { ushort v; argument >> v; return v; }
    case 'i': { int v; argument >> v; return v; }
    case 'u': { uint v; argument >> v; return v; }
    case 'x': { qlonglong v; argument >> v; return v; }
    case 't': { qulonglong v; argument >> v; return v; }
    case 'd': { double v; argument >> v; return v; }
    case 's': { QString v; argument >> v; return v; }
    case 'o': { QDBusObjectPath v; argument >> v; return QVariant::fromValue(v); }
    case 'g': { QDBusSignature v; argument >> v; return QVariant::fromValue(v); }
    case 'h': { QDBusUnixFileDescriptor v; argument >> v; return QVariant::fromValue(v); }
    case 'a':
        // QDBusDemarshaller::currentType() reports byte and string arrays as
        // BasicType (they map to QByteArray / QStringList), so 'ay' and 'as'
        // land here rather than in the ArrayType branch of demarshall.
        if (signature == QLatin1String("ay")) { QByteArray v; argument >> v; return v; }
        if (signature == QLatin1String("as")) { QStringList v; argument >> v; return v; }
        return QVariant();
    default:
        return QVariant();
    }
}

static QJsonValue unsupportedElement(const QString &signature)
{
    return QJsonValue(QStringLiteral("<unsupported D-Bus type '%1'>").arg(signature));
}

// JSON object keys are strings. QVariant::toString() knows nothing about
// QDBusObjectPath / QDBusSignature and returns "" for them, so unwrap those.
static QString mapKeyToString(const QVariant &key)
{
    if (key.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return key.value<QDBusObjectPath>().path();
    if (key.metaType() == QMetaType::fromType<QDBusSignature>())
        return key.value<QDBusSignature>().signature();
    return key.toString();
}

// Demarshall the current element into `out`. Returns false when the element
// was NOT consumed (its wire type is unknown to extractBasic): `out` then
// holds an explanatory marker and the enclosing container loop must stop,
// because the iterator did not advance and atEnd() would never become true.
// Containers always count as consumed (begin*() already moved the parent's
// iterator past them), so a truncated container is reported as consumed to
// its parent and the rest of the reply is still decoded.
static bool demarshallElement(const QDBusArgument &argument, QJsonValue &out)
{
    switch (argument.currentType()) {
    case QDBusArgument::BasicType: {
        const QVariant value = extractBasic(argument);
        if (!value.isValid()) {
            out = unsupportedElement(argument.currentSignature());
            return false;
        }
        out = DBusBridge::variantToJson(value);
        return true;
    }
    case QDBusArgument::VariantType: {
        QDBusVariant value;
        argument >> value;
        out = DBusBridge::variantToJson(value.variant());
        return true;
    }
    case QDBusArgument::ArrayType:
    case QDBusArgument::StructureType: {
        const bool isArray = argument.currentType() == QDBusArgument::ArrayType;
        QJsonArray array;
        if (isArray)
            argument.beginArray();
        else
            argument.beginStructure();
        while (!argument.atEnd()) {
            QJsonValue element;
            const bool consumed = demarshallElement(argument, element);
            array.append(element);
            if (!consumed)
                break;
        }
        if (isArray)
            argument.endArray();
        else
            argument.endStructure();
        out = array;
        return true;
    }
    case QDBusArgument::MapType: {
        QJsonObject object;
        argument.beginMap();
        while (!argument.atEnd()) {
            argument.beginMapEntry();
            const QVariant key = extractBasic(argument); // map keys are basic by spec
            QJsonValue value;
            const bool consumed = key.isValid() && demarshallElement(argument, value);
            object.insert(key.isValid() ? mapKeyToString(key) : QStringLiteral("<unsupported key>"),
                          value);
            argument.endMapEntry();
            if (!consumed)
                break;
        }
        argument.endMap();
        out = object;
        return true;
    }
    default:
        out = unsupportedElement(argument.currentSignature());
        return false;
    }
}

QJsonValue DBusBridge::demarshall(const QDBusArgument &argument)
{
    QJsonValue out;
    demarshallElement(argument, out);
    return out;
}
