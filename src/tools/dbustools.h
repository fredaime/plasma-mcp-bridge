// SPDX-License-Identifier: MIT
#pragma once

#include "mcp/tool.h"

class DBusBridge;
class CallPolicy;

// Enumerate the well-known names currently on a bus.
class DBusListServicesTool : public Tool
{
public:
    DBusListServicesTool(DBusBridge *bridge, const CallPolicy *policy)
        : m_bridge(bridge), m_policy(policy) {}
    QString name() const override;
    QString description() const override;
    QJsonObject inputSchema() const override;
    ToolResult call(const QJsonObject &arguments) override;

private:
    DBusBridge *m_bridge;
    const CallPolicy *m_policy;
};

// Return the introspection XML for an object, so an agent can discover the
// interfaces, methods and signals it exposes before calling them.
class DBusIntrospectTool : public Tool
{
public:
    DBusIntrospectTool(DBusBridge *bridge, const CallPolicy *policy)
        : m_bridge(bridge), m_policy(policy) {}
    QString name() const override;
    QString description() const override;
    QJsonObject inputSchema() const override;
    ToolResult call(const QJsonObject &arguments) override;

private:
    DBusBridge *m_bridge;
    const CallPolicy *m_policy;
};

// The generic bridge: invoke any method on any D-Bus object. This is what
// exposes the entire Plasma/KDE and freedesktop automation surface.
class DBusCallTool : public Tool
{
public:
    DBusCallTool(DBusBridge *bridge, const CallPolicy *policy, int defaultTimeoutMs)
        : m_bridge(bridge), m_policy(policy), m_defaultTimeoutMs(defaultTimeoutMs) {}
    QString name() const override;
    QString description() const override;
    QJsonObject inputSchema() const override;
    ToolResult call(const QJsonObject &arguments) override;

private:
    DBusBridge *m_bridge;
    const CallPolicy *m_policy;
    int m_defaultTimeoutMs;
};
