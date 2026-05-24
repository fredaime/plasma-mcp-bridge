// SPDX-License-Identifier: MIT
#include "mcp/server.h"

#include "mcp/jsonrpc.h"
#include "mcp/stdiotransport.h"
#include "mcp/tool.h"
#include "mcp/toolregistry.h"

#include <QJsonArray>

#ifndef PLASMA_MCP_BRIDGE_VERSION
#define PLASMA_MCP_BRIDGE_VERSION "0.0.0"
#endif

namespace {
// Last protocol revision this server is known to speak. When a client asks
// for a version we recognise we echo it back; otherwise we fall back to this.
constexpr auto kDefaultProtocolVersion = "2024-11-05";
} // namespace

Server::Server(StdioTransport *transport, ToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_registry(registry)
{
    connect(m_transport, &StdioTransport::messageReceived, this, &Server::onMessage);
}

void Server::onMessage(const QJsonObject &message)
{
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonValue id = message.value(QStringLiteral("id"));
    const bool isRequest = !id.isUndefined() && !id.isNull();
    const QJsonObject params = message.value(QStringLiteral("params")).toObject();

    if (method.isEmpty())
        return; // A response or malformed frame; nothing to dispatch.

    if (method == QLatin1String("initialize")) {
        handleInitialize(id, params);
    } else if (method == QLatin1String("notifications/initialized")) {
        m_initialized = true;
    } else if (method == QLatin1String("ping")) {
        m_transport->send(mcp::jsonrpc::makeResult(id, QJsonObject{}));
    } else if (method == QLatin1String("tools/list")) {
        handleToolsList(id);
    } else if (method == QLatin1String("tools/call")) {
        handleToolsCall(id, params);
    } else if (isRequest) {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::MethodNotFound,
            QStringLiteral("Method not found: %1").arg(method)));
    }
    // Unknown notifications are silently ignored, per JSON-RPC.
}

void Server::handleInitialize(const QJsonValue &id, const QJsonObject &params)
{
    QString protocolVersion = params.value(QStringLiteral("protocolVersion")).toString();
    if (protocolVersion.isEmpty())
        protocolVersion = QLatin1String(kDefaultProtocolVersion);

    QJsonObject toolsCapability;
    toolsCapability.insert(QStringLiteral("listChanged"), false);
    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("tools"), toolsCapability);

    QJsonObject serverInfo;
    serverInfo.insert(QStringLiteral("name"), QStringLiteral("plasma-mcp-bridge"));
    serverInfo.insert(QStringLiteral("version"), QStringLiteral(PLASMA_MCP_BRIDGE_VERSION));

    QJsonObject result;
    result.insert(QStringLiteral("protocolVersion"), protocolVersion);
    result.insert(QStringLiteral("capabilities"), capabilities);
    result.insert(QStringLiteral("serverInfo"), serverInfo);

    m_transport->send(mcp::jsonrpc::makeResult(id, result));
}

void Server::handleToolsList(const QJsonValue &id)
{
    QJsonObject result;
    result.insert(QStringLiteral("tools"), m_registry->toJson());
    m_transport->send(mcp::jsonrpc::makeResult(id, result));
}

void Server::handleToolsCall(const QJsonValue &id, const QJsonObject &params)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    const QJsonObject arguments = params.value(QStringLiteral("arguments")).toObject();

    Tool *tool = m_registry->find(name);
    if (!tool) {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::InvalidParams, QStringLiteral("Unknown tool: %1").arg(name)));
        return;
    }

    const ToolResult toolResult = tool->call(arguments);

    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), toolResult.text);

    QJsonObject result;
    result.insert(QStringLiteral("content"), QJsonArray{content});
    result.insert(QStringLiteral("isError"), toolResult.isError);

    m_transport->send(mcp::jsonrpc::makeResult(id, result));
}
