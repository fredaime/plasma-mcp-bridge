// SPDX-License-Identifier: MIT
#pragma once

#include "core/backend.h"

class CallPolicy;

// Built-in backend exposing the three generic D-Bus tools (list_services,
// introspect, call). This is the universal automation surface: any Plasma or
// freedesktop service can be reached through it, within the CallPolicy.
class DBusBackend : public Backend
{
public:
    explicit DBusBackend(const CallPolicy *policy) : m_policy(policy) {}
    QString name() const override;
    QString description() const override;
    void registerTools(ToolRegistry *registry, const BridgeContext &context) override;

private:
    const CallPolicy *m_policy;
};
