// SPDX-License-Identifier: MIT
#pragma once

#include <QString>
#include <QStringList>

class QDBusConnection;

// Which interface and input signature a method call targets, according to
// the object's introspection data. Internal to the core (not installed).
struct MethodResolution {
    enum State {
        Unavailable, // introspection failed, or the method is not declared
        Unique,      // exactly one interface declares the method
        Ambiguous,   // no interface given and several interfaces declare it
    };
    State state = Unavailable;
    // Unique: the declaring interface. Otherwise: the caller's interface,
    // possibly empty.
    QString interface;
    // Unique only: one complete D-Bus type per input argument.
    QStringList inSignature;
};

// Pure parser, used by resolveMethod. `interface` may be empty.
MethodResolution resolveMethodFromXml(const QString &xml, const QString &interface,
                                      const QString &method);

// Introspects service/path (one round-trip, no cache) and resolves `method`.
MethodResolution resolveMethod(const QDBusConnection &bus, const QString &service,
                               const QString &path, const QString &interface,
                               const QString &method);
