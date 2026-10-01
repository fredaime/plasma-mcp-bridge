// SPDX-License-Identifier: MIT
#include "mcp/server.h"

#include "mcp/jsonrpc.h"
#include "mcp/protocolversion.h"
#include "mcp/stdiotransport.h"
#include "mcp/tool.h"
#include "mcp/toolregistry.h"
#include "mcp/toolrunner.h"

#include <QJsonArray>

#ifndef PLASMA_MCP_BRIDGE_VERSION
#define PLASMA_MCP_BRIDGE_VERSION "0.0.0"
#endif

Server::Server(StdioTransport *transport, ToolRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_transport(transport)
    , m_registry(registry)
    , m_runner(new ToolRunner(this))
{
    connect(m_transport, &StdioTransport::messageReceived, this, &Server::onMessage);
    connect(m_transport, &StdioTransport::invalidFrame, this, &Server::onInvalidFrame);
    connect(m_runner, &ToolRunner::resultReady, this, &Server::sendToolResult);
}

void Server::setSerializedTools(const QSet<QString> &names)
{
    m_serializedTools = names;
}

void Server::onInvalidFrame(int code)
{
    m_transport->send(mcp::jsonrpc::makeError(
        QJsonValue(), code,
        code == mcp::jsonrpc::ParseError
            ? QStringLiteral("Parse error")
            : QStringLiteral("Invalid request: a frame must be one JSON-RPC object (batches "
                             "are not supported)")));
}

void Server::onMessage(const QJsonObject &message)
{
    const QJsonValue method = message.value(QStringLiteral("method"));
    const QJsonValue id = message.value(QStringLiteral("id"));

    if (!message.contains(QStringLiteral("method"))
        && (message.contains(QStringLiteral("result"))
            || message.contains(QStringLiteral("error")))) {
        // This server sends no requests: a response from the client is stray.
        qInfo("plasma-mcp-bridge: ignoring a JSON-RPC response from the client (id %s)",
              qUtf8Printable(mcp::jsonrpc::idText(id)));
        return;
    }

    const bool hasId = message.contains(QStringLiteral("id"));
    const bool validId = id.isString() || id.isDouble();
    if (!method.isString() || method.toString().isEmpty() || (hasId && !validId)) {
        m_transport->send(mcp::jsonrpc::makeError(
            validId ? id : QJsonValue(), mcp::jsonrpc::InvalidRequest,
            QStringLiteral("Invalid request: 'method' must be a non-empty string and 'id' a "
                           "string or a number")));
        return;
    }

    const QString name = method.toString();
    const QJsonValue params = message.value(QStringLiteral("params"));
    if (!hasId) {
        handleNotification(name, params.toObject());
        return;
    }
    if (m_runner->isInFlight(id)) {
        // Exactly one response per id: the call already running keeps it.
        qInfo("plasma-mcp-bridge: ignoring request %s: a request with this id is still in flight",
              qUtf8Printable(mcp::jsonrpc::idText(id)));
        return;
    }

    if (name == QLatin1String("initialize")) {
        handleInitialize(id, params.toObject());
    } else if (name == QLatin1String("ping")) {
        m_transport->send(mcp::jsonrpc::makeResult(id, QJsonObject{}));
    } else if (name == QLatin1String("tools/list")) {
        handleToolsList(id);
    } else if (name == QLatin1String("tools/call")) {
        handleToolsCall(id, params);
    } else {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::MethodNotFound, QStringLiteral("Method not found: %1").arg(name)));
    }
}

void Server::handleNotification(const QString &method, const QJsonObject &params)
{
    // notifications/initialized and every other notification: nothing to do.
    if (method != QLatin1String("notifications/cancelled"))
        return;
    const QJsonValue requestId = params.value(QStringLiteral("requestId"));
    if (m_runner->cancel(requestId))
        qInfo("plasma-mcp-bridge: cancelled request %s",
              qUtf8Printable(mcp::jsonrpc::idText(requestId)));
    else
        qInfo("plasma-mcp-bridge: ignoring cancellation of request %s (unknown or finished)",
              qUtf8Printable(mcp::jsonrpc::idText(requestId)));
}

void Server::handleInitialize(const QJsonValue &id, const QJsonObject &params)
{
    const QString protocolVersion =
        mcp::negotiateProtocolVersion(params.value(QStringLiteral("protocolVersion")));

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

void Server::handleToolsCall(const QJsonValue &id, const QJsonValue &params)
{
    const QJsonValue name = params.toObject().value(QStringLiteral("name"));
    const QJsonValue arguments = params.toObject().value(QStringLiteral("arguments"));
    if (!params.isObject() || !name.isString()
        || !(arguments.isUndefined() || arguments.isNull() || arguments.isObject())) {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::InvalidParams,
            QStringLiteral("tools/call takes {\"name\": string, \"arguments\": object}")));
        return;
    }

    Tool *tool = m_registry->find(name.toString());
    if (!tool) {
        m_transport->send(mcp::jsonrpc::makeError(
            id, mcp::jsonrpc::InvalidParams,
            QStringLiteral("Unknown tool: %1").arg(name.toString())));
        return;
    }

    m_runner->submit(id, tool, arguments.toObject(),
                     m_serializedTools.contains(name.toString()));
}

void Server::sendToolResult(const QJsonValue &id, const QString &text, bool isError)
{
    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), text);

    QJsonObject result;
    result.insert(QStringLiteral("content"), QJsonArray{content});
    result.insert(QStringLiteral("isError"), isError);

    m_transport->send(mcp::jsonrpc::makeResult(id, result));
}
