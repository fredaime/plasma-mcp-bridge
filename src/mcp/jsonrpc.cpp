// SPDX-License-Identifier: MIT
#include "mcp/jsonrpc.h"

namespace mcp::jsonrpc {

QJsonObject makeResult(const QJsonValue &id, const QJsonValue &result)
{
    QJsonObject msg;
    msg.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    msg.insert(QStringLiteral("id"), id);
    msg.insert(QStringLiteral("result"), result);
    return msg;
}

QJsonObject makeError(const QJsonValue &id, int code, const QString &message,
                      const QJsonValue &data)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    if (!data.isNull() && !data.isUndefined())
        error.insert(QStringLiteral("data"), data);

    QJsonObject msg;
    msg.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    msg.insert(QStringLiteral("id"), id);
    msg.insert(QStringLiteral("error"), error);
    return msg;
}

} // namespace mcp::jsonrpc
