// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>

class QThreadPool;
class Tool;

// Runs tools/call off the main thread: built-in tools on a pool of four
// workers, serialized tools (plugins) on a pool of one, in arrival order.
// Results come back on the runner's thread (the main thread), the only one
// that writes stdout. The pools are children of the runner: destroying it
// waits for running calls, so it must only be destroyed with none in flight.
class ToolRunner : public QObject
{
    Q_OBJECT
public:
    explicit ToolRunner(QObject *parent = nullptr);

    void submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments, bool serialized);

Q_SIGNALS:
    void resultReady(const QJsonValue &id, const QString &text, bool isError);

private:
    void finish(const QJsonValue &id, const QString &text, bool isError);

    QThreadPool *m_pool;
    QThreadPool *m_serialPool;
    QSet<QString> m_inFlight; // mcp::jsonrpc::idText of each call in flight
};
