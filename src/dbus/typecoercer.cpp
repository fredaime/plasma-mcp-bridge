// SPDX-License-Identifier: MIT
#include "dbus/typecoercer.h"

#include "dbus/dbusbridge.h"

#include <QByteArray>
#include <QDBusObjectPath>
#include <QDBusSignature>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>
#include <limits>

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

} // namespace

namespace TypeCoercer {

bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error)
{
    if (signature.size() == 1) {
        const QChar type = signature.at(0);
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
                *out = QVariant::fromValue(QDBusSignature(value.toString()));
            }
            return true;
        default:
            break;
        }
    } else if (signature == QLatin1String("ay")) {
        return coerceByteArray(value, out, error);
    } else if (signature == QLatin1String("as")) {
        const QJsonArray array = value.toArray();
        QStringList strings;
        bool ok = value.isArray();
        for (const QJsonValue &item : array) {
            ok = ok && item.isString();
            strings.append(item.toString());
        }
        if (!ok) {
            *error = QStringLiteral("expected an array of strings, got %1").arg(jsonText(value));
            return false;
        }
        *out = strings;
        return true;
    }

    *out = DBusBridge::jsonToVariant(value);
    return true;
}

} // namespace TypeCoercer
