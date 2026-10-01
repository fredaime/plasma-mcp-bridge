// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace TypeCoercer {

// Convert one JSON argument into the QVariant to send for the D-Bus type
// `signature` (one complete type), driven by the parsed signature: basic
// types, arrays, maps, structs and variants ({"@dbus": "<type>", "value": …}
// forces a variant's type, otherwise the natural mapping applies). The value
// is checked in full before anything is built; on error, returns false with
// *error set ("[1]: field 0: …"), and nothing must be sent.
bool coerce(const QJsonValue &value, const QString &signature, QVariant *out, QString *error);

} // namespace TypeCoercer
