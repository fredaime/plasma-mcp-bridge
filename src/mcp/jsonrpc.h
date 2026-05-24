// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace mcp::jsonrpc {

constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int InternalError = -32603;

QJsonObject makeResult(const QJsonValue &id, const QJsonValue &result);
QJsonObject makeError(const QJsonValue &id, int code, const QString &message,
                      const QJsonValue &data = QJsonValue());

} // namespace mcp::jsonrpc
