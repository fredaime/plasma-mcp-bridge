// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>

class StdioTransport;
class ToolRegistry;

// Implements the MCP request handlers (initialize, tools/list, tools/call,
// ping) on top of a JSON-RPC stream.
class Server : public QObject
{
    Q_OBJECT
public:
    Server(StdioTransport *transport, ToolRegistry *registry, QObject *parent = nullptr);

private:
    void onMessage(const QJsonObject &message);
    void handleInitialize(const QJsonValue &id, const QJsonObject &params);
    void handleToolsList(const QJsonValue &id);
    void handleToolsCall(const QJsonValue &id, const QJsonObject &params);

    StdioTransport *m_transport;
    ToolRegistry *m_registry;
};
