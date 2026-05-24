// SPDX-License-Identifier: MIT
#include "mcp/stdiotransport.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QSocketNotifier>

#include <cstdio>
#include <unistd.h>

StdioTransport::StdioTransport(QObject *parent)
    : QObject(parent)
{
}

void StdioTransport::start()
{
    m_notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &StdioTransport::onReadable);
}

void StdioTransport::onReadable()
{
    char chunk[8192];
    const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof(chunk));
    if (n < 0)
        return; // EAGAIN/EINTR: the notifier will fire again.
    if (n == 0) {
        m_notifier->setEnabled(false);
        Q_EMIT closed();
        return;
    }

    m_buffer.append(chunk, static_cast<int>(n));

    int newline;
    while ((newline = m_buffer.indexOf('\n')) != -1) {
        QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("plasma-mcp-bridge: dropping invalid JSON-RPC frame: %s",
                     parseError.errorString().toUtf8().constData());
            continue;
        }
        Q_EMIT messageReceived(doc.object());
    }
}

void StdioTransport::send(const QJsonObject &message)
{
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    ::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stdout);
    ::fflush(stdout);
}
