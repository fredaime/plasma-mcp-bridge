// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

struct PolicyDecision {
    bool allowed = true;
    // The rule that decided, empty when none matched: "system-bus".
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

    // Replaces the configuration. False, with *error set, when an option is
    // invalid; the previous configuration is then kept.
    bool configure(const Options &options, QString *error);

    bool systemBusAllowed() const;

    // Error text returned to the agent for a refused call.
    static QString refusal(const PolicyDecision &decision);

private:
    bool m_allowSystemBus = false;
};
