// SPDX-License-Identifier: MIT
// Minimal plugin for the test-suite: tools that sleep, to observe how the
// bridge schedules plugin tools, and a tool whose name clashes with a
// built-in one.
#include "core/backend.h"
#include "core/plugin.h"
#include "mcp/tool.h"
#include "mcp/toolregistry.h"

#include <QDateTime>
#include <QObject>
#include <QThread>

namespace {

class SleepTool : public Tool
{
public:
    explicit SleepTool(const QString &name) : m_name(name) {}
    QString name() const override { return m_name; }
    QString description() const override
    {
        return QStringLiteral("Test: sleep 'ms' milliseconds; reply \"<start>-<end>\" in ms "
                              "since the epoch.");
    }
    QJsonObject inputSchema() const override
    {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
    }
    ToolResult call(const QJsonObject &arguments) override
    {
        qInfo("test-plugin: %s ran", qUtf8Printable(m_name));
        const qint64 start = QDateTime::currentMSecsSinceEpoch();
        QThread::msleep(static_cast<unsigned long>(arguments.value(QStringLiteral("ms")).toInt()));
        return ToolResult::ok(
            QStringLiteral("%1-%2").arg(start).arg(QDateTime::currentMSecsSinceEpoch()));
    }

private:
    QString m_name;
};

class ClashTool : public SleepTool
{
public:
    ClashTool() : SleepTool(QStringLiteral("dbus_call")) {}
    QString description() const override
    {
        return QStringLiteral("Test: clashes with a built-in tool name.");
    }
};

class ThreadTool : public Tool
{
public:
    QString name() const override { return QStringLiteral("test_thread"); }
    QString description() const override
    {
        return QStringLiteral("Test: reply how many calls this thread has run, this one "
                              "included (thread ids are recycled, a counter is not).");
    }
    QJsonObject inputSchema() const override
    {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
    }
    ToolResult call(const QJsonObject &) override
    {
        thread_local int calls = 0;
        return ToolResult::ok(QString::number(++calls));
    }
};

class TestBackend : public Backend
{
public:
    QString name() const override { return QStringLiteral("test"); }
    QString description() const override { return QStringLiteral("Test-suite plugin."); }
    void registerTools(ToolRegistry *registry, const BridgeContext &) override
    {
        registry->add(std::make_unique<SleepTool>(QStringLiteral("test_sleep_a")));
        registry->add(std::make_unique<SleepTool>(QStringLiteral("test_sleep_b")));
        registry->add(std::make_unique<ClashTool>());
        registry->add(std::make_unique<ThreadTool>());
    }
};

} // namespace

class TestPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PLASMA_MCP_BRIDGE_PLUGIN_IID)
    Q_INTERFACES(PluginInterface)

public:
    std::vector<std::unique_ptr<Backend>> createBackends() override
    {
        std::vector<std::unique_ptr<Backend>> backends;
        backends.push_back(std::make_unique<TestBackend>());
        return backends;
    }
};

#include "testplugin.moc"
