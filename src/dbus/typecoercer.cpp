// SPDX-License-Identifier: MIT
#include "dbus/typecoercer.h"

#include "dbus/dbusbridge.h"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusSignature>
#include <QDBusVariant>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>
#include <limits>
#include <tuple>

namespace {

constexpr double kInt64Limit = 9223372036854775808.0; // 2^63

QString jsonText(const QJsonValue &value)
{
    const QByteArray json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

// Sign + magnitude, so the whole int64 and uint64 ranges fit.
struct ParsedInteger {
    bool negative = false;
    quint64 magnitude = 0;
};

// A JSON number that is integral and fits in int64 (Qt's parser keeps such
// integers exact), or a decimal string of any size (needed for large uint64).
bool parseInteger(const QJsonValue &value, ParsedInteger *out, QString *error)
{
    if (value.isString()) {
        QString text = value.toString().trimmed();
        out->negative = text.startsWith(QLatin1Char('-'));
        if (out->negative)
            text.remove(0, 1);
        bool ok = false;
        if (!text.isEmpty() && text.at(0).isDigit())
            out->magnitude = text.toULongLong(&ok, 10);
        if (!ok) {
            *error = QStringLiteral("%1 is not an integer").arg(jsonText(value));
            return false;
        }
        return true;
    }
    if (!value.isDouble()) {
        *error = QStringLiteral("expected an integer, got %1").arg(jsonText(value));
        return false;
    }
    // An integer that fits in int64 is held exactly by Qt's JSON parser, even
    // where toDouble() would round it (e.g. INT64_MAX). toInteger() returns it
    // whatever the default; two different defaults detect "not such a value".
    const qint64 exactA = value.toInteger(0);
    const qint64 exactB = value.toInteger(1);
    if (exactA == exactB) {
        out->negative = exactA < 0;
        out->magnitude = exactA < 0 ? static_cast<quint64>(-(exactA + 1)) + 1
                                    : static_cast<quint64>(exactA);
        return true;
    }
    const double d = value.toDouble();
    if (!std::isfinite(d) || std::floor(d) != d) {
        *error = QStringLiteral("%1 is not an integer").arg(jsonText(value));
        return false;
    }
    if (std::fabs(d) >= kInt64Limit) {
        *error = QStringLiteral("%1 is beyond the int64 range, where JSON numbers lose digits: "
                                "pass it as a decimal string").arg(jsonText(value));
        return false;
    }
    // Fallback only: integral values within int64 were handled exactly above.
    const qint64 exact = static_cast<qint64>(d);
    out->negative = exact < 0;
    out->magnitude = exact < 0 ? static_cast<quint64>(-(exact + 1)) + 1
                               : static_cast<quint64>(exact);
    return true;
}

struct IntegerRange {
    qint64 min;
    quint64 max;
};

bool integerRange(QChar type, IntegerRange *range)
{
    switch (type.toLatin1()) {
    case 'y': *range = {0, 255}; return true;
    case 'n': *range = {-32768, 32767}; return true;
    case 'q': *range = {0, 65535}; return true;
    case 'i': *range = {std::numeric_limits<qint32>::min(), std::numeric_limits<qint32>::max()}; return true;
    case 'u': *range = {0, std::numeric_limits<quint32>::max()}; return true;
    case 'x': *range = {std::numeric_limits<qint64>::min(),
                        static_cast<quint64>(std::numeric_limits<qint64>::max())}; return true;
    case 't': *range = {0, std::numeric_limits<quint64>::max()}; return true;
    default: return false;
    }
}

bool inRange(const ParsedInteger &n, const IntegerRange &range)
{
    if (!n.negative || n.magnitude == 0)
        return n.magnitude <= range.max;
    if (range.min >= 0)
        return false;
    // -(min + 1) + 1 avoids overflowing on INT64_MIN.
    return n.magnitude <= static_cast<quint64>(-(range.min + 1)) + 1;
}

qint64 signedValue(const ParsedInteger &n)
{
    if (!n.negative || n.magnitude == 0)
        return static_cast<qint64>(n.magnitude);
    return -static_cast<qint64>(n.magnitude - 1) - 1;
}

QVariant integerVariant(QChar type, const ParsedInteger &n)
{
    switch (type.toLatin1()) {
    case 'y': return QVariant::fromValue(static_cast<uchar>(n.magnitude));
    case 'n': return QVariant::fromValue(static_cast<short>(signedValue(n)));
    case 'q': return QVariant::fromValue(static_cast<ushort>(n.magnitude));
    case 'i': return QVariant::fromValue(static_cast<int>(signedValue(n)));
    case 'u': return QVariant::fromValue(static_cast<uint>(n.magnitude));
    case 'x': return QVariant::fromValue(static_cast<qlonglong>(signedValue(n)));
    default:  return QVariant::fromValue(static_cast<qulonglong>(n.magnitude)); // 't'
    }
}

bool coerceInteger(const QJsonValue &value, QChar type, QVariant *out, QString *error)
{
    IntegerRange range{};
    integerRange(type, &range);
    ParsedInteger n;
    if (!parseInteger(value, &n, error))
        return false;
    if (!inRange(n, range)) {
        *error = QStringLiteral("%1 out of range [%2,%3]")
                     .arg(jsonText(value)).arg(range.min).arg(range.max);
        return false;
    }
    *out = integerVariant(type, n);
    return true;
}

bool isObjectPath(const QString &path)
{
    static const QRegularExpression pattern(QStringLiteral("^/([A-Za-z0-9_]+(/[A-Za-z0-9_]+)*)?$"));
    return pattern.match(path).hasMatch();
}

bool coerceByteArray(const QJsonValue &value, QVariant *out, QString *error)
{
    if (value.isString()) {
        const auto decoded = QByteArray::fromBase64Encoding(
            value.toString().toLatin1(),
            QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
            *error = QStringLiteral("%1 is not valid base64").arg(jsonText(value));
            return false;
        }
        *out = decoded.decoded;
        return true;
    }
    if (!value.isArray()) {
        *error = QStringLiteral("expected an array of bytes or a base64 string, got %1")
                     .arg(jsonText(value));
        return false;
    }
    QByteArray bytes;
    const QJsonArray array = value.toArray();
    for (int i = 0; i < array.size(); ++i) {
        QVariant byte;
        QString byteError;
        if (!coerceInteger(array.at(i), QLatin1Char('y'), &byte, &byteError)) {
            *error = QStringLiteral("[%1]: %2").arg(i).arg(byteError);
            return false;
        }
        bytes.append(static_cast<char>(byte.value<uchar>()));
    }
    *out = bytes;
    return true;
}

// ------------------------------------------------------------ signatures

constexpr int kMaxDepth = 64; // D-Bus allows 32 array plus 32 struct levels

bool isBasicType(QChar type)
{
    return QStringLiteral("ybnqiuxtdsogh").contains(type);
}

// End of the complete type that starts at `pos`, or -1 when `sig` is
// malformed there.
qsizetype completeTypeEnd(const QString &sig, qsizetype pos, int depth)
{
    if (pos >= sig.size() || depth > kMaxDepth)
        return -1;
    const QChar type = sig.at(pos);
    if (isBasicType(type) || type == QLatin1Char('v'))
        return pos + 1;
    if (type == QLatin1Char('a')) {
        if (pos + 1 < sig.size() && sig.at(pos + 1) == QLatin1Char('{')) {
            const qsizetype key = pos + 2;
            if (key >= sig.size() || !isBasicType(sig.at(key)))
                return -1;
            const qsizetype end = completeTypeEnd(sig, key + 1, depth + 1);
            if (end < 0 || end >= sig.size() || sig.at(end) != QLatin1Char('}'))
                return -1;
            return end + 1;
        }
        return completeTypeEnd(sig, pos + 1, depth + 1);
    }
    if (type == QLatin1Char('(')) {
        qsizetype p = pos + 1;
        int fields = 0;
        while (p < sig.size() && sig.at(p) != QLatin1Char(')')) {
            p = completeTypeEnd(sig, p, depth + 1);
            if (p < 0)
                return -1;
            ++fields;
        }
        if (p >= sig.size() || fields == 0)
            return -1;
        return p + 1;
    }
    return -1;
}

// The complete types of `sig`, in order; empty when `sig` is malformed.
QStringList splitTypes(const QString &sig)
{
    QStringList types;
    qsizetype p = 0;
    while (p < sig.size()) {
        const qsizetype end = completeTypeEnd(sig, p, 0);
        if (end < 0)
            return {};
        types.append(sig.mid(p, end - p));
        p = end;
    }
    return types;
}

bool isSingleCompleteType(const QString &sig)
{
    return splitTypes(sig).size() == 1;
}

bool isDict(const QString &sig)
{
    return sig.startsWith(QLatin1String("a{"));
}

QChar dictKey(const QString &sig)
{
    return sig.at(2);
}

QString dictValue(const QString &sig)
{
    return sig.mid(3, sig.size() - 4);
}

QStringList structFields(const QString &sig)
{
    return splitTypes(sig.mid(1, sig.size() - 2));
}

// ------------------------------------------------------------ element types

// A struct whose fields QtDBus marshals in order. Qt 6.4's QDBusArgument has
// no std::tuple support, so the curated struct types are built on this.
template <typename... T>
struct DBusStruct {
    std::tuple<T...> fields;
};

template <typename... T>
QDBusArgument &operator<<(QDBusArgument &argument, const DBusStruct<T...> &value)
{
    argument.beginStructure();
    std::apply([&argument](const auto &...field) { (argument << ... << field); }, value.fields);
    argument.endStructure();
    return argument;
}

template <typename... T>
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusStruct<T...> &value)
{
    argument.beginStructure();
    std::apply([&argument](auto &...field) { (argument >> ... >> field); }, value.fields);
    argument.endStructure();
    return argument;
}

// The element types beginArray() and beginMap() accept: one registered
// QMetaType per array element / map value signature. QtDBus knows the native
// ones; the curated ones are registered here once (thread-safe static), also
// those recent Qt versions register themselves but Qt 6.4 does not. Written
// out on purpose: QDBusMetaType::typeToSignature is internal API. Extend case
// by case, with a test.
QMetaType elementMetaType(const QString &sig)
{
    static const QHash<QString, QMetaType> table = [] {
        QHash<QString, QMetaType> types{
            {QStringLiteral("y"), QMetaType::fromType<uchar>()},
            {QStringLiteral("b"), QMetaType::fromType<bool>()},
            {QStringLiteral("n"), QMetaType::fromType<short>()},
            {QStringLiteral("q"), QMetaType::fromType<ushort>()},
            {QStringLiteral("i"), QMetaType::fromType<int>()},
            {QStringLiteral("u"), QMetaType::fromType<uint>()},
            {QStringLiteral("x"), QMetaType::fromType<qlonglong>()},
            {QStringLiteral("t"), QMetaType::fromType<qulonglong>()},
            {QStringLiteral("d"), QMetaType::fromType<double>()},
            {QStringLiteral("s"), QMetaType::fromType<QString>()},
            {QStringLiteral("o"), QMetaType::fromType<QDBusObjectPath>()},
            {QStringLiteral("g"), QMetaType::fromType<QDBusSignature>()},
            {QStringLiteral("v"), QMetaType::fromType<QDBusVariant>()},
            {QStringLiteral("as"), QMetaType::fromType<QStringList>()},
            {QStringLiteral("ay"), QMetaType::fromType<QByteArray>()},
            {QStringLiteral("av"), QMetaType::fromType<QVariantList>()},
            {QStringLiteral("a{sv}"), QMetaType::fromType<QVariantMap>()},
            {QStringLiteral("ao"), QMetaType::fromType<QList<QDBusObjectPath>>()},
            {QStringLiteral("ag"), QMetaType::fromType<QList<QDBusSignature>>()},
            {QStringLiteral("ab"), QMetaType::fromType<QList<bool>>()},
            {QStringLiteral("an"), QMetaType::fromType<QList<short>>()},
            {QStringLiteral("aq"), QMetaType::fromType<QList<ushort>>()},
            {QStringLiteral("ai"), QMetaType::fromType<QList<int>>()},
            {QStringLiteral("au"), QMetaType::fromType<QList<uint>>()},
            {QStringLiteral("ax"), QMetaType::fromType<QList<qlonglong>>()},
            {QStringLiteral("at"), QMetaType::fromType<QList<qulonglong>>()},
            {QStringLiteral("ad"), QMetaType::fromType<QList<double>>()},
        };
        types.insert(QStringLiteral("a{ss}"), qDBusRegisterMetaType<QMap<QString, QString>>());
        types.insert(QStringLiteral("aas"), qDBusRegisterMetaType<QList<QStringList>>());
        types.insert(QStringLiteral("aay"), qDBusRegisterMetaType<QList<QByteArray>>());
        types.insert(QStringLiteral("aa{sv}"), qDBusRegisterMetaType<QList<QVariantMap>>());
        types.insert(QStringLiteral("a{sa{sv}}"),
                     qDBusRegisterMetaType<QMap<QString, QVariantMap>>());
        types.insert(QStringLiteral("aai"), qDBusRegisterMetaType<QList<QList<int>>>());
        types.insert(QStringLiteral("(si)"), qDBusRegisterMetaType<DBusStruct<QString, int>>());
        types.insert(QStringLiteral("(ss)"),
                     qDBusRegisterMetaType<DBusStruct<QString, QString>>());
        types.insert(QStringLiteral("(sss)"),
                     qDBusRegisterMetaType<DBusStruct<QString, QString, QString>>());
        types.insert(QStringLiteral("(ii)"), qDBusRegisterMetaType<DBusStruct<int, int>>());
        types.insert(QStringLiteral("(ai)"), qDBusRegisterMetaType<DBusStruct<QList<int>>>());
        types.insert(QStringLiteral("(oa{sv})"),
                     qDBusRegisterMetaType<DBusStruct<QDBusObjectPath, QVariantMap>>());
        types.insert(QStringLiteral("(iss)"),
                     qDBusRegisterMetaType<DBusStruct<int, QString, QString>>());
        return types;
    }();
    return table.value(sig);
}

// ------------------------------------------------------------ basic values

// Basic types: the strict conversion of PR3a.
bool coerceBasic(const QJsonValue &value, QChar type, QVariant *out, QString *error)
{
    IntegerRange unused{};
    if (integerRange(type, &unused))
        return coerceInteger(value, type, out, error);
    switch (type.toLatin1()) {
    case 'b':
        if (!value.isBool()) {
            *error = QStringLiteral("expected a boolean, got %1").arg(jsonText(value));
            return false;
        }
        *out = value.toBool();
        return true;
    case 'd':
        if (!value.isDouble()) {
            *error = QStringLiteral("expected a number, got %1").arg(jsonText(value));
            return false;
        }
        *out = value.toDouble();
        return true;
    case 's':
    case 'o':
    case 'g':
        if (!value.isString()) {
            *error = QStringLiteral("expected a string, got %1").arg(jsonText(value));
            return false;
        }
        if (type == QLatin1Char('s')) {
            *out = value.toString();
        } else if (type == QLatin1Char('o')) {
            if (!isObjectPath(value.toString())) {
                *error = QStringLiteral("%1 is not a valid object path").arg(jsonText(value));
                return false;
            }
            *out = QVariant::fromValue(QDBusObjectPath(value.toString()));
        } else {
            const QString signature = value.toString();
            if (signature.size() > 255 || (!signature.isEmpty() && splitTypes(signature).isEmpty())) {
                *error = QStringLiteral("%1 is not a valid D-Bus signature").arg(jsonText(value));
                return false;
            }
            *out = QVariant::fromValue(QDBusSignature(signature));
        }
        return true;
    case 'h':
        *error = QStringLiteral("unix fd arguments are not supported (they cannot travel over MCP)");
        return false;
    default:
        *error = QStringLiteral("'%1' is not a basic D-Bus type").arg(type);
        return false;
    }
}

// A JSON object key as the JSON value a map key of `type` is read from:
// numbers and booleans are written as text in JSON keys.
QJsonValue keyValue(const QString &key, QChar type)
{
    switch (type.toLatin1()) {
    case 'd': {
        bool ok = false;
        const double number = key.toDouble(&ok);
        return ok ? QJsonValue(number) : QJsonValue(key);
    }
    case 'b':
        if (key == QLatin1String("true"))
            return true;
        if (key == QLatin1String("false"))
            return false;
        return key;
    default:
        return key; // integers: parseInteger reads decimal strings
    }
}

// ------------------------------------------------------------ pass 1: check

QString prefixed(const QString &where, const QString &error)
{
    return where + QStringLiteral(": ") + error;
}

QString checkValue(const QJsonValue &value, const QString &sig, int depth);

// What a variant may hold: {"@dbus": "<type>", "value": …} for an explicit
// type, otherwise the natural mapping of the JSON value.
QString checkVariant(const QJsonValue &value, int depth)
{
    if (depth > kMaxDepth)
        return QStringLiteral("nested deeper than %1 levels").arg(kMaxDepth);
    const QJsonObject object = value.toObject();
    if (value.isObject() && object.contains(QStringLiteral("@dbus"))) {
        const QJsonValue inner = object.value(QStringLiteral("@dbus"));
        if (!isSingleCompleteType(inner.toString()))
            return QStringLiteral("%1 is not a single complete D-Bus type").arg(jsonText(inner));
        return checkValue(object.value(QStringLiteral("value")), inner.toString(), depth + 1);
    }
    switch (value.type()) {
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QString error = checkVariant(array.at(i), depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("[%1]").arg(i), error);
        }
        return QString();
    }
    case QJsonValue::Object:
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString error = checkVariant(it.value(), depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("['%1']").arg(it.key()), error);
        }
        return QString();
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return QStringLiteral("null cannot be sent in a D-Bus variant");
    default:
        return QString();
    }
}

// Pass 1: checks `value` against the complete type `sig`; returns the error,
// empty when the value can be written. Builds nothing.
QString checkValue(const QJsonValue &value, const QString &sig, int depth)
{
    if (depth > kMaxDepth)
        return QStringLiteral("nested deeper than %1 levels").arg(kMaxDepth);
    const QChar type = sig.at(0);
    if (sig.size() == 1 && isBasicType(type)) {
        QVariant unused;
        QString error;
        return coerceBasic(value, type, &unused, &error) ? QString() : error;
    }
    if (sig == QLatin1String("v"))
        return checkVariant(value, depth + 1);
    if (sig == QLatin1String("ay")) {
        QVariant unused;
        QString error;
        return coerceByteArray(value, &unused, &error) ? QString() : error;
    }
    if (sig == QLatin1String("as")) {
        bool ok = value.isArray();
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            ok = ok && item.isString();
        return ok ? QString()
                  : QStringLiteral("expected an array of strings, got %1").arg(jsonText(value));
    }
    if (isDict(sig)) {
        const QChar key = dictKey(sig);
        const QString valueSig = dictValue(sig);
        if (key == QLatin1Char('h'))
            return QStringLiteral(
                "unix fd arguments are not supported (they cannot travel over MCP)");
        if (!elementMetaType(valueSig).isValid())
            return QStringLiteral("unsupported D-Bus signature '%1' (map value)").arg(valueSig);
        if (!value.isObject())
            return QStringLiteral("expected an object for '%1', got %2").arg(sig, jsonText(value));
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            QVariant unused;
            QString error;
            if (!coerceBasic(keyValue(it.key(), key), key, &unused, &error))
                return prefixed(QStringLiteral("key '%1'").arg(it.key()), error);
            error = checkValue(it.value(), valueSig, depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("['%1']").arg(it.key()), error);
        }
        return QString();
    }
    if (type == QLatin1Char('a')) {
        const QString element = sig.mid(1);
        if (!elementMetaType(element).isValid())
            return QStringLiteral("unsupported D-Bus signature '%1' (array element)").arg(element);
        if (!value.isArray())
            return QStringLiteral("expected an array for '%1', got %2").arg(sig, jsonText(value));
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            const QString error = checkValue(array.at(i), element, depth + 1);
            if (!error.isEmpty())
                return prefixed(QStringLiteral("[%1]").arg(i), error);
        }
        return QString();
    }
    // A struct: a JSON array with one item per field.
    const QStringList fields = structFields(sig);
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != fields.size())
        return QStringLiteral("expected an array of %1 fields for '%2', got %3")
            .arg(fields.size())
            .arg(sig, jsonText(value));
    for (int i = 0; i < fields.size(); ++i) {
        const QString error = checkValue(array.at(i), fields.at(i), depth + 1);
        if (!error.isEmpty())
            return prefixed(QStringLiteral("field %1").arg(i), error);
    }
    return QString();
}

// ------------------------------------------------------------ pass 2: build
// Only after checkValue() succeeded: nothing below can fail, so every
// begin*() meets its end*() (libdbus aborts the whole process when an
// array's content does not match the element type given to beginArray()).

QVariant basicValue(const QJsonValue &value, QChar type)
{
    QVariant out;
    QString unused;
    coerceBasic(value, type, &out, &unused);
    return out;
}

void appendBasic(QDBusArgument &argument, const QVariant &value, QChar type)
{
    switch (type.toLatin1()) {
    case 'y': argument << value.value<uchar>(); break;
    case 'b': argument << value.toBool(); break;
    case 'n': argument << value.value<short>(); break;
    case 'q': argument << value.value<ushort>(); break;
    case 'i': argument << value.value<int>(); break;
    case 'u': argument << value.value<uint>(); break;
    case 'x': argument << value.value<qlonglong>(); break;
    case 't': argument << value.value<qulonglong>(); break;
    case 'd': argument << value.toDouble(); break;
    case 's': argument << value.toString(); break;
    case 'o': argument << value.value<QDBusObjectPath>(); break;
    default: argument << value.value<QDBusSignature>(); break; // 'g'
    }
}

QVariant build(const QJsonValue &value, const QString &sig);

// The content of a variant: the explicit "@dbus" type, otherwise the natural
// mapping (scalars as DBusBridge::jsonToVariant maps them; arrays as 'av' and
// objects as 'a{sv}', whose items may carry "@dbus" again).
QVariant variantContent(const QJsonValue &value)
{
    const QJsonObject object = value.toObject();
    if (value.isObject() && object.contains(QStringLiteral("@dbus")))
        return build(object.value(QStringLiteral("value")),
                     object.value(QStringLiteral("@dbus")).toString());
    if (value.isArray()) {
        QVariantList list;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            list.append(QVariant::fromValue(QDBusVariant(variantContent(item))));
        return list;
    }
    if (value.isObject()) {
        QVariantMap map;
        for (auto it = object.begin(); it != object.end(); ++it)
            map.insert(it.key(), variantContent(it.value()));
        return map;
    }
    return DBusBridge::jsonToVariant(value);
}

void write(QDBusArgument &argument, const QJsonValue &value, const QString &sig)
{
    const QChar type = sig.at(0);
    if (sig.size() == 1 && isBasicType(type)) {
        appendBasic(argument, basicValue(value, type), type);
    } else if (sig == QLatin1String("v")) {
        argument << QDBusVariant(variantContent(value));
    } else if (sig == QLatin1String("ay")) {
        argument << build(value, sig).toByteArray();
    } else if (isDict(sig)) {
        const QChar key = dictKey(sig);
        const QString valueSig = dictValue(sig);
        argument.beginMap(elementMetaType(QString(key)), elementMetaType(valueSig));
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            argument.beginMapEntry();
            appendBasic(argument, basicValue(keyValue(it.key(), key), key), key);
            write(argument, it.value(), valueSig);
            argument.endMapEntry();
        }
        argument.endMap();
    } else if (type == QLatin1Char('a')) {
        const QString element = sig.mid(1);
        argument.beginArray(elementMetaType(element));
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            write(argument, item, element);
        argument.endArray();
    } else {
        const QStringList fields = structFields(sig);
        const QJsonArray array = value.toArray();
        argument.beginStructure();
        for (int i = 0; i < fields.size(); ++i)
            write(argument, array.at(i), fields.at(i));
        argument.endStructure();
    }
}

// Pass 2 at the root: typed QVariants where QtDBus has a native type,
// otherwise a QDBusArgument written by hand.
QVariant build(const QJsonValue &value, const QString &sig)
{
    const QChar type = sig.at(0);
    if (sig.size() == 1 && isBasicType(type))
        return basicValue(value, type);
    if (sig == QLatin1String("v"))
        return QVariant::fromValue(QDBusVariant(variantContent(value)));
    if (sig == QLatin1String("ay")) {
        QVariant out;
        QString unused;
        coerceByteArray(value, &out, &unused);
        return out;
    }
    if (sig == QLatin1String("as")) {
        QStringList strings;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
            strings.append(item.toString());
        return strings;
    }
    QDBusArgument argument;
    write(argument, value, sig);
    return QVariant::fromValue(argument);
}

} // namespace

namespace TypeCoercer {

bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error)
{
    if (!isSingleCompleteType(signature)) {
        *error = QStringLiteral("'%1' is not a single complete D-Bus type").arg(signature);
        return false;
    }
    const QString problem = checkValue(value, signature, 0);
    if (!problem.isEmpty()) {
        *error = problem;
        return false;
    }
    *out = build(value, signature);
    return true;
}

} // namespace TypeCoercer
