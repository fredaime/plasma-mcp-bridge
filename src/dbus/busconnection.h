// SPDX-License-Identifier: MIT
#pragma once

#include <QDBusConnection>
#include <QString>

// "system" selects the system bus, anything else the session bus. The tools
// validate the name first (m5). Internal to the core (not installed).
inline QDBusConnection busConnection(const QString &busName)
{
    return busName == QLatin1String("system") ? QDBusConnection::systemBus()
                                              : QDBusConnection::sessionBus();
}
