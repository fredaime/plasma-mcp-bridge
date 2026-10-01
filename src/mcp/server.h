// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>

class StdioTransport;
class ToolRegistry;
class ToolRunner;

// Implements the MCP request handlers on top of a JSON-RPC stream, for both
// eras at once: a request whose params._meta names a protocol version is
// served statelessly according to MCP 2026-07-28; anything else (including
// every initialize) follows the historical lifecycle. Runs on the main
// thread: it answers at once everything but tools/call, which goes to a
// ToolRunner, so replies may come out of order (they carry the request id).
class Server : public QObject
{
    Q_OBJECT
public:
    Server(StdioTransport *transport, ToolRegistry *registry, QObject *parent = nullptr);

    // Tools contributed by plugins: they run one at a time (a plugin need not
    // be reentrant), beside the built-in tools.
    void setSerializedTools(const QSet<QString> &names);

    // The client is gone (stdin closed or stdout broken): no more tools/call
    // is accepted; results that arrive within two seconds are still written.
    // Emits finished() once nothing is in flight. If a call is still running
    // after two seconds, flushes and exits the process (see server.cpp).
    void shutdown();

Q_SIGNALS:
    void finished();

private:
    void onMessage(const QJsonObject &message);
    void onInvalidFrame(int code);
    void handleNotification(const QString &method, const QJsonObject &params);
    // MCP 2026-07-28: stateless, per-request metadata (see server.cpp).
    void handleModernRequest(const QJsonValue &id, const QString &method,
                             const QJsonObject &params);
    void handleDiscover(const QJsonValue &id);
    void handleListen(const QJsonValue &id);
    // Graceful teardown of the open subscriptions/listen streams.
    void closeSubscriptions();
    void handleInitialize(const QJsonValue &id, const QJsonObject &params);
    void handleToolsList(const QJsonValue &id);
    void handleToolsCall(const QJsonValue &id, const QJsonValue &params, bool modern);
    void handleModernToolsList(const QJsonValue &id, const QJsonObject &params);
    void sendToolResult(const QJsonValue &id, const QString &text, bool isError);

    StdioTransport *m_transport;
    ToolRegistry *m_registry;
    ToolRunner *m_runner;
    QSet<QString> m_serializedTools;
    QSet<QString> m_modernCalls; // idText of the modern tools/call in flight
    QHash<QString, QJsonValue> m_listens; // open subscriptions/listen, by idText
    bool m_closing = false;
};
