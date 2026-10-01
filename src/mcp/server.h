// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>

class StdioTransport;
class ToolRegistry;
class ToolRunner;

// Implements the MCP request handlers on top of a JSON-RPC stream. Runs on
// the main thread: it answers initialize, ping, tools/list, notifications
// and protocol errors at once, and hands every tools/call to a ToolRunner,
// so replies may come out of order (they carry the request id).
class Server : public QObject
{
    Q_OBJECT
public:
    Server(StdioTransport *transport, ToolRegistry *registry, QObject *parent = nullptr);

    // Tools contributed by plugins: they run one at a time (a plugin need not
    // be reentrant), beside the built-in tools.
    void setSerializedTools(const QSet<QString> &names);

private:
    void onMessage(const QJsonObject &message);
    void onInvalidFrame(int code);
    void handleNotification(const QString &method, const QJsonObject &params);
    void handleInitialize(const QJsonValue &id, const QJsonObject &params);
    void handleToolsList(const QJsonValue &id);
    void handleToolsCall(const QJsonValue &id, const QJsonValue &params);
    void sendToolResult(const QJsonValue &id, const QString &text, bool isError);

    StdioTransport *m_transport;
    ToolRegistry *m_registry;
    ToolRunner *m_runner;
    QSet<QString> m_serializedTools;
};
