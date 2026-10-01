// SPDX-License-Identifier: MIT
#include "mcp/stdiotransport.h"

#include "mcp/jsonrpc.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QSocketNotifier>

#include <cerrno>
#include <cstdio>
#include <cstring>
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
        if (parseError.error != QJsonParseError::NoError) {
            qInfo("plasma-mcp-bridge: invalid JSON frame: %s",
                  qUtf8Printable(parseError.errorString()));
            Q_EMIT invalidFrame(mcp::jsonrpc::ParseError);
            continue;
        }
        if (!doc.isObject()) {
            Q_EMIT invalidFrame(mcp::jsonrpc::InvalidRequest);
            continue;
        }
        Q_EMIT messageReceived(doc.object());
    }
}

void StdioTransport::send(const QJsonObject &message)
{
    if (m_broken)
        return;
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stdout)
            != static_cast<size_t>(data.size())
        || std::fflush(stdout) != 0) {
        m_broken = true;
        qInfo("plasma-mcp-bridge: cannot write to stdout (%s); shutting down",
              std::strerror(errno));
        if (m_notifier)
            m_notifier->setEnabled(false);
        Q_EMIT closed();
    }
}
