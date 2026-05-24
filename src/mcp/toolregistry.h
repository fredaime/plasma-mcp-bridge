// SPDX-License-Identifier: MIT
#pragma once

#include "mcp/tool.h"

#include <QHash>
#include <QJsonArray>
#include <QString>

#include <memory>
#include <vector>

class ToolRegistry
{
public:
    void add(std::unique_ptr<Tool> tool);
    Tool *find(const QString &name) const;
    int count() const { return static_cast<int>(m_tools.size()); }

    // Serialised as the "tools" array of an MCP tools/list response.
    QJsonArray toJson() const;

private:
    std::vector<std::unique_ptr<Tool>> m_tools;
    QHash<QString, Tool *> m_index;
};
