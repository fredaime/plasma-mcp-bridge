// SPDX-License-Identifier: MIT
#pragma once

#include "mcp/tool.h"

class DBusBridge;

// Enumerate the well-known names currently on a bus.
class DBusListServicesTool : public Tool
{
public:
    explicit DBusListServicesTool(DBusBridge *bridge) : m_bridge(bridge) {}
    QString name() const override;
    QString description() const override;
    QJsonObject inputSchema() const override;
    ToolResult call(const QJsonObject &arguments) override;

private:
    DBusBridge *m_bridge;
};

// Return the introspection XML for an object, so an agent can discover the
// interfaces, methods and signals it exposes before calling them.
class DBusIntrospectTool : public Tool
{
public:
    explicit DBusIntrospectTool(DBusBridge *bridge) : m_bridge(bridge) {}
    QString name() const override;
    QString description() const override;
    QJsonObject inputSchema() const override;
    ToolResult call(const QJsonObject &arguments) override;

private:
    DBusBridge *m_bridge;
};

// The generic bridge: invoke any method on any D-Bus object. This is what
// exposes the entire Plasma/KDE and freedesktop automation surface.
class DBusCallTool : public Tool
{
public:
    explicit DBusCallTool(DBusBridge *bridge) : m_bridge(bridge) {}
    QString name() const override;
    QString description() const override;
    QJsonObject inputSchema() const override;
    ToolResult call(const QJsonObject &arguments) override;

private:
    DBusBridge *m_bridge;
};
