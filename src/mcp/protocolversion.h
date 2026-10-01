// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace mcp {

// MCP revisions this server speaks, oldest first. 2025-03-26 is left out on
// purpose: it requires receiving JSON-RPC batches, which later revisions
// removed and this server rejects.
QStringList supportedProtocolVersions();

// The version answered to `initialize`: the requested one when supported;
// otherwise the highest supported one older than the request (a client
// capped at 2025-03-26 gets 2024-11-05); otherwise the latest. Never fails.
QString negotiateProtocolVersion(const QJsonValue &requested);

} // namespace mcp
