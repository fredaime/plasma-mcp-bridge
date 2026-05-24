// SPDX-License-Identifier: MIT
#include "mcp/toolregistry.h"
#include "mcp/tool.h"

void ToolRegistry::add(std::unique_ptr<Tool> tool)
{
    if (!tool)
        return;
    m_index.insert(tool->name(), tool.get());
    m_tools.push_back(std::move(tool));
}

Tool *ToolRegistry::find(const QString &name) const
{
    return m_index.value(name, nullptr);
}

QJsonArray ToolRegistry::toJson() const
{
    QJsonArray array;
    for (const auto &tool : m_tools) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), tool->name());
        object.insert(QStringLiteral("description"), tool->description());
        object.insert(QStringLiteral("inputSchema"), tool->inputSchema());
        array.append(object);
    }
    return array;
}
