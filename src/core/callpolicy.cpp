// SPDX-License-Identifier: MIT
#include "core/callpolicy.h"

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

QString CallPolicy::refusal(const PolicyDecision &decision)
{
    Q_UNUSED(decision); // only "system-bus" so far
    return QStringLiteral("Refused by policy: the system bus is disabled "
                          "(start plasma-mcp-bridge with --allow-system-bus)");
}
