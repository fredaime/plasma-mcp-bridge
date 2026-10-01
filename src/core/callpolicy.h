// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// One dbus_call as the policy sees it. `interface` is empty when it is
// unknown: not given, and the introspection data does not name exactly one
// interface declaring the method.
struct CallTarget {
    QString bus;
    QString service;
    QString path;
    QString interface;
    QString method;
};

struct PolicyDecision {
    bool allowed = true;
    // The rule that decided, empty when none matched: "system-bus",
    // "unique-name", "deny:<pattern>", "allow:<pattern>", "builtin:<pattern>"
    // or "default-deny".
    QString rule;
};

// Best-effort guard rails for the D-Bus tools. NOT a security boundary (see
// README, "Built-in guard rails"): it stops known destructive calls an agent
// may propose, nothing more. Immutable once configured, so it may be read
// from several threads. Internal to the core (not installed).
class CallPolicy
{
public:
    struct Options {
        bool allowSystemBus = false;   // --allow-system-bus
        bool allowUniqueNames = false; // --allow-unique-names
        bool defaultDeny = false;      // --default-deny
        QStringList deny;              // --deny SERVICE:INTERFACE.METHOD
        QStringList allow;             // --allow SERVICE:INTERFACE.METHOD
    };

    // Defaults: system bus and unique names refused, built-in denylist on.
    CallPolicy();

    // Replaces the configuration. False, with *error set, when a pattern is
    // malformed; the previous configuration is then kept.
    bool configure(const Options &options, QString *error);

    bool systemBusAllowed() const;
    // False when the bus or the destination alone is refused: the caller
    // then skips resolving the interface (no round-trip for a refused call).
    bool destinationAllowed(const QString &bus, const QString &service) const;
    // Order: system bus, unique name, --deny, --allow, built-in denylist,
    // --default-deny.
    PolicyDecision evaluate(const CallTarget &target) const;

    // Error text returned to the agent for a refused call.
    static QString refusal(const PolicyDecision &decision);
    // "plasma-mcp-bridge: audit: <allow|deny> <bus> <service> <path>
    // <interface>.<method>[ <rule>]", interface "*" when unknown. The tool
    // has checked that no name contains a space or a line break.
    static QString auditLine(const CallTarget &target, const PolicyDecision &decision);
    // SERVICE:INTERFACE.METHOD patterns; '*' matches any run of characters.
    static QStringList builtinDenylist();

private:
    struct Pattern {
        QString text;
        QString service;
        QString interface;
        QString method;
    };
    static bool parsePattern(const QString &text, Pattern *out);
    static bool matches(const Pattern &pattern, const CallTarget &target,
                        bool unknownInterfaceMatches);

    bool m_allowSystemBus = false;
    bool m_allowUniqueNames = false;
    bool m_defaultDeny = false;
    QVector<Pattern> m_deny;
    QVector<Pattern> m_allow;
    QVector<Pattern> m_builtin;
};
