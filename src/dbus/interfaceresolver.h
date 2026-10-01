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
    // The introspection XML, when the introspection succeeded.
    QString introspection;
    // Set when the introspection itself failed with a D-Bus error.
    QString errorName;
    QString errorMessage;

    // The service did not answer the introspection in time: the call would
    // wait as long again, so callers give up at once (timeout_ms bounds the
    // whole call on a hung service, not each round-trip).
    bool timedOut() const
    {
        return errorName == QLatin1String("org.freedesktop.DBus.Error.NoReply")
            || errorName == QLatin1String("org.freedesktop.DBus.Error.Timeout");
    }
};

// Pure parser, used by resolveMethod. `interface` may be empty.
MethodResolution resolveMethodFromXml(const QString &xml, const QString &interface,
                                      const QString &method);

// The D-Bus type of `property` on `interface`, from the object's own
// introspection data (child node descriptions ignored); empty when the
// property is not declared.
QString propertyTypeFromXml(const QString &xml, const QString &interface,
                            const QString &property);

// Introspects service/path (one round-trip of at most timeoutMs, no cache)
// and resolves `method`.
MethodResolution resolveMethod(const QDBusConnection &bus, const QString &service,
                               const QString &path, const QString &interface,
                               const QString &method, int timeoutMs);
