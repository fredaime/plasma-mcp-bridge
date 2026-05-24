// SPDX-License-Identifier: MIT
#pragma once

#include "mcp/tool.h"

class DBusBridge;

// High-level convenience wrapper over the freedesktop Notifications spec
// (org.freedesktop.Notifications). Works on Plasma and any other compliant
// desktop; serves as the template for richer Plasma-specific tools.
class NotifyTool : public Tool
{
public:
    explicit NotifyTool(DBusBridge *bridge) : m_bridge(bridge) {}
    QString name() const override;
    QString description() const override;
    QJsonObject inputSchema() const override;
    ToolResult call(const QJsonObject &arguments) override;

private:
    DBusBridge *m_bridge;
};
