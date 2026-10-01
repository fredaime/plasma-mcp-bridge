// SPDX-License-Identifier: MIT
#include "mcp/protocolversion.h"

#include <QRegularExpression>

namespace mcp {

QStringList supportedProtocolVersions()
{
    return {QStringLiteral("2024-11-05"), QStringLiteral("2025-06-18"),
            QStringLiteral("2025-11-25")};
}

QString negotiateProtocolVersion(const QJsonValue &requested)
{
    const QStringList supported = supportedProtocolVersions();
    const QString version = requested.toString();
    if (supported.contains(version))
        return version;
    // Revisions are dates: between well-formed ones, string order is date order.
    static const QRegularExpression date(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    if (date.match(version).hasMatch()) {
        for (auto it = supported.crbegin(); it != supported.crend(); ++it) {
            if (*it < version)
                return *it;
        }
    }
    return supported.last();
}

} // namespace mcp
