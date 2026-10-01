// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

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

    // False, and nothing runs, when a call with this id is still in flight.
    bool submit(const QJsonValue &id, Tool *tool, const QJsonObject &arguments, bool serialized);
    // Tool::call() cannot be interrupted: a cancelled call is skipped if it
    // has not started, and its result is dropped otherwise. False when the
    // id is not in flight (unknown or finished).
    bool cancel(const QJsonValue &id);
    bool isInFlight(const QJsonValue &id) const;

Q_SIGNALS:
    // Not emitted for a cancelled call.
    void resultReady(const QJsonValue &id, const QString &text, bool isError);

private:
    void finish(const QJsonValue &id, const QString &text, bool isError);

    QThreadPool *m_pool;
    QThreadPool *m_serialPool;
    // Calls in flight by mcp::jsonrpc::idText, with their cancel flag.
    QHash<QString, std::shared_ptr<std::atomic_bool>> m_inFlight;
};
