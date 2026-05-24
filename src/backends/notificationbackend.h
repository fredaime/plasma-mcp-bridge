// SPDX-License-Identifier: MIT
#pragma once

#include "core/backend.h"

// Built-in backend wrapping the freedesktop Notifications service. Kept
// separate from the generic D-Bus backend because it represents a different
// abstraction level (a single high-level capability rather than raw bus access).
class NotificationBackend : public Backend
{
public:
    QString name() const override;
    QString description() const override;
    void registerTools(ToolRegistry *registry, const BridgeContext &context) override;
};
