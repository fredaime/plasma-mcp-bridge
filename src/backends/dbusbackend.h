// SPDX-License-Identifier: MIT
#pragma once

#include "core/backend.h"

// Built-in backend exposing the three generic D-Bus tools (list_services,
// introspect, call). This is the universal automation surface: any Plasma or
// freedesktop service can be reached through it.
class DBusBackend : public Backend
{
public:
    QString name() const override;
    QString description() const override;
    void registerTools(ToolRegistry *registry, const BridgeContext &context) override;
};
