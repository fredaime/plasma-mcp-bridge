// SPDX-License-Identifier: MIT
#include "mcp/jsonrpc.h"

#include <QJsonArray>
#include <QJsonDocument>

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

QString idText(const QJsonValue &id)
{
    const QByteArray json = QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

QJsonObject makeNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject msg;
    msg.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    msg.insert(QStringLiteral("method"), method);
    msg.insert(QStringLiteral("params"), params);
    return msg;
}

} // namespace mcp::jsonrpc
