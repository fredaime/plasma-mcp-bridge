// SPDX-License-Identifier: MIT
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

class QSocketNotifier;

// Newline-delimited JSON-RPC over stdin/stdout, as used by the MCP stdio
// transport. Anything written to stdout must be a protocol message; all
// diagnostics go to stderr (via the q* logging macros).
class StdioTransport : public QObject
{
    Q_OBJECT
public:
    explicit StdioTransport(QObject *parent = nullptr);

    void start();
    void send(const QJsonObject &message);

Q_SIGNALS:
    void messageReceived(const QJsonObject &message);
    void closed();

private:
    void onReadable();

    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_buffer;
};
