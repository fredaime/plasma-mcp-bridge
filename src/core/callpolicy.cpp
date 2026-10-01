// SPDX-License-Identifier: MIT
#include "core/callpolicy.h"

namespace {

// '*' matches any run of characters, dots included; every other character
// matches itself, case-sensitively.
bool globMatch(const QString &pattern, const QString &text)
{
    qsizetype p = 0;
    qsizetype t = 0;
    qsizetype star = -1;
    qsizetype resume = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern.at(p) == QLatin1Char('*')) {
            star = p++;
            resume = t;
        } else if (p < pattern.size() && pattern.at(p) == text.at(t)) {
            ++p;
            ++t;
        } else if (star >= 0) {
            p = star + 1;
            t = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern.at(p) == QLatin1Char('*'))
        ++p;
    return p == pattern.size();
}

// Known destructive or code-running methods, checked by introspection on
// Plasma 6.7 and systemd (Ubuntu 26.10). Prefix wildcards also catch the
// *WithFlags, *Replace, ... variants.
const char *const kBuiltinDenylist[] = {
    "org.freedesktop.login1:org.freedesktop.login1.Manager.PowerOff*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Reboot*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Halt*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Suspend*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Hibernate*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.HybridSleep*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Sleep*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.KExec*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.Terminate*",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.KillSession",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.KillUser",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.ScheduleShutdown",
    "org.freedesktop.login1:org.freedesktop.login1.Manager.SetWallMessage",
    "org.kde.KWin:org.kde.kwin.Scripting.*",
    "org.kde.ksmserver:org.kde.KSMServerInterface.closeSession",
    "org.kde.ksmserver:org.kde.KSMServerInterface.logout*",
    "org.kde.Shutdown:org.kde.Shutdown.*",
    "org.kde.plasmashell:org.kde.PlasmaShell.evaluateScript",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.StartUnit*",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.StartTransientUnit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.RestartUnit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.ReloadOrRestartUnit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.EnqueueUnitJob",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.KillUnit*",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.SetEnvironment",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.UnsetAndSetEnvironment",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.PowerOff",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.Reboot",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.SoftReboot",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.Halt",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.KExec",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.Exit",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Manager.SwitchRoot",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.Start",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.Restart",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.ReloadOrRestart",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.Kill",
    "org.freedesktop.systemd1:org.freedesktop.systemd1.Unit.EnqueueJob",
    "org.freedesktop.DBus:org.freedesktop.DBus.UpdateActivationEnvironment",
};

} // namespace

CallPolicy::CallPolicy()
{
    for (const QString &text : builtinDenylist()) {
        Pattern pattern;
        const bool valid = parsePattern(text, &pattern);
        Q_ASSERT(valid);
        Q_UNUSED(valid);
        m_builtin.append(pattern);
    }
}

bool CallPolicy::configure(const Options &options, QString *error)
{
    Q_UNUSED(error); // no option can be invalid yet
    m_allowSystemBus = options.allowSystemBus;
    return true;
}

bool CallPolicy::systemBusAllowed() const
{
    return m_allowSystemBus;
}

bool CallPolicy::destinationAllowed(const QString &bus, const QString &service) const
{
    if (bus == QLatin1String("system") && !m_allowSystemBus)
        return false;
    return !service.startsWith(QLatin1Char(':'));
}

PolicyDecision CallPolicy::evaluate(const CallTarget &target) const
{
    if (target.bus == QLatin1String("system") && !m_allowSystemBus)
        return {false, QStringLiteral("system-bus")};
    // Otherwise the denylist could be bypassed with the unique name that
    // dbus_list_services shows.
    if (target.service.startsWith(QLatin1Char(':')))
        return {false, QStringLiteral("unique-name")};
    for (const Pattern &pattern : m_builtin) {
        if (matches(pattern, target, true))
            return {false, QStringLiteral("builtin:") + pattern.text};
    }
    return {true, QString()};
}

QString CallPolicy::refusal(const PolicyDecision &decision)
{
    const QString &rule = decision.rule;
    if (rule == QLatin1String("system-bus"))
        return QStringLiteral("Refused by policy: the system bus is disabled "
                              "(start plasma-mcp-bridge with --allow-system-bus)");
    if (rule == QLatin1String("unique-name"))
        return QStringLiteral("Refused by policy: a unique connection name (:N.M) is not "
                              "accepted as destination; use the service's well-known name (or "
                              "start plasma-mcp-bridge with --allow-unique-names)");
    return QStringLiteral("Refused by policy: built-in denylist entry %1 (give 'interface' and "
                          "start plasma-mcp-bridge with --allow %1 to permit it)")
        .arg(rule.mid(8)); // after "builtin:"
}

QStringList CallPolicy::builtinDenylist()
{
    QStringList list;
    for (const char *entry : kBuiltinDenylist)
        list.append(QString::fromLatin1(entry));
    return list;
}

// SERVICE:INTERFACE.METHOD. The last ':' ends the service (a unique name
// starts with ':'), the last '.' starts the method; no part may be empty.
bool CallPolicy::parsePattern(const QString &text, Pattern *out)
{
    const qsizetype colon = text.lastIndexOf(QLatin1Char(':'));
    const qsizetype dot = text.lastIndexOf(QLatin1Char('.'));
    if (colon <= 0 || dot <= colon + 1 || dot == text.size() - 1)
        return false;
    out->text = text;
    out->service = text.left(colon);
    out->interface = text.mid(colon + 1, dot - colon - 1);
    out->method = text.mid(dot + 1);
    return true;
}

// An unknown interface gets the strictest reading: a refusing rule matches it
// whatever its interface, an allowing rule only when its interface is '*'.
bool CallPolicy::matches(const Pattern &pattern, const CallTarget &target,
                         bool unknownInterfaceMatches)
{
    if (!globMatch(pattern.service, target.service) || !globMatch(pattern.method, target.method))
        return false;
    if (target.interface.isEmpty())
        return unknownInterfaceMatches || pattern.interface == QLatin1String("*");
    return globMatch(pattern.interface, target.interface);
}
