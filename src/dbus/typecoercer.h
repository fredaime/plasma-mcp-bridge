// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace TypeCoercer {

// Convert one JSON argument into the QVariant to send for the D-Bus type
// `signature` (one complete type). Basic types, 'as' and 'ay' are converted
// strictly: a wrong JSON type, an out-of-range or non-integral number or bad
// base64 returns false with *error set, and nothing must be sent. Other types
// are converted loosely with DBusBridge::jsonToVariant (for now).
bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error);

} // namespace TypeCoercer
