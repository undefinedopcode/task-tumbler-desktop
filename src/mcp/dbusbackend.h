#pragma once

#include "mcpserver.h"

namespace tumbler::mcp {

// Calls the running Task Tumbler over D-Bus, starting it in the tray first
// if it isn't running.
class DBusBackend : public Backend
{
public:
    QVariant call(const QString &method, const QVariantList &args, QString *error) override;

private:
    bool ensureRunning(QString *error);
};

} // namespace tumbler::mcp
