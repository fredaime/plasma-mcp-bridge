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

bool ToolRunner::submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments,
                        bool serialized)
{
    const QString key = mcp::jsonrpc::idText(id);
    if (m_inFlight.contains(key))
        return false;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_inFlight.insert(key, cancelled);
    QThreadPool *pool = serialized ? m_serialPool : m_pool;
    pool->start(QRunnable::create([this, id, tool, arguments, cancelled] {
        ToolResult result;
        if (!cancelled->load()) // cancelled while waiting in the queue: never starts
            result = tool->call(arguments);
        QMetaObject::invokeMethod(
            this, [this, id, result] { finish(id, result.text, result.isError); },
            Qt::QueuedConnection);
    }));
    return true;
}

bool ToolRunner::cancel(const QJsonValue &id)
{
    const auto it = m_inFlight.constFind(mcp::jsonrpc::idText(id));
    if (it == m_inFlight.constEnd())
        return false;
    it.value()->store(true);
    return true;
}

bool ToolRunner::isInFlight(const QJsonValue &id) const
{
    return m_inFlight.contains(mcp::jsonrpc::idText(id));
}

void ToolRunner::finish(const QJsonValue &id, const QString &text, bool isError)
{
    const std::shared_ptr<std::atomic_bool> cancelled = m_inFlight.take(mcp::jsonrpc::idText(id));
    if (cancelled && !cancelled->load())
        Q_EMIT resultReady(id, text, isError);
}
