// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QString>

struct ToolResult {
    QString text;
    bool isError = false;

    static ToolResult ok(const QString &text) { return {text, false}; }
    static ToolResult failure(const QString &text) { return {text, true}; }
};

// A single MCP tool. Implementations describe themselves with a JSON Schema
// (inputSchema) and execute synchronously in call().
class Tool
{
public:
    virtual ~Tool() = default;

    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QJsonObject inputSchema() const = 0;
    virtual ToolResult call(const QJsonObject &arguments) = 0;
};
