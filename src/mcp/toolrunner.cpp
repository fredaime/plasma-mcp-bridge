// SPDX-License-Identifier: MIT
#include "mcp/toolrunner.h"

#include "mcp/jsonrpc.h"
#include "mcp/tool.h"

#include <QMetaObject>
#include <QRunnable>
#include <QThreadPool>

ToolRunner::ToolRunner(QObject *parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
    , m_serialPool(new QThreadPool(this))
{
    m_pool->setMaxThreadCount(4);
    m_serialPool->setMaxThreadCount(1);
}

void ToolRunner::submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments,
                        bool serialized)
{
    m_inFlight.insert(mcp::jsonrpc::idText(id));
    QThreadPool *pool = serialized ? m_serialPool : m_pool;
    pool->start(QRunnable::create([this, id, tool, arguments] {
        const ToolResult result = tool->call(arguments);
        QMetaObject::invokeMethod(
            this, [this, id, result] { finish(id, result.text, result.isError); },
            Qt::QueuedConnection);
    }));
}

void ToolRunner::finish(const QJsonValue &id, const QString &text, bool isError)
{
    m_inFlight.remove(mcp::jsonrpc::idText(id));
    Q_EMIT resultReady(id, text, isError);
}
