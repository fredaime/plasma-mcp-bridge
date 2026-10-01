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
    // "unique-name" or "builtin:<pattern>".
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
        bool allowSystemBus = false; // --allow-system-bus
    };

    // Defaults: system bus and unique names refused, built-in denylist on.
    CallPolicy();

    // Replaces the configuration. False, with *error set, when an option is
    // invalid; the previous configuration is then kept.
    bool configure(const Options &options, QString *error);

    bool systemBusAllowed() const;
    // False when the bus or the destination alone is refused: the caller
    // then skips resolving the interface (no round-trip for a refused call).
    bool destinationAllowed(const QString &bus, const QString &service) const;
    PolicyDecision evaluate(const CallTarget &target) const;

    // Error text returned to the agent for a refused call.
    static QString refusal(const PolicyDecision &decision);
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
    QVector<Pattern> m_builtin;
};
