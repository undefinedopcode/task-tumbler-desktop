#pragma once

#include "mcpserver.h"

#include <memory>

namespace tumbler::mcp {

// Calls the running Task Tumbler over its local socket (ipc.h), starting it
// in the tray first if it isn't running.
class SocketBackend : public Backend
{
public:
    // `dbPath`: the database of the instance to reach; empty for the usual one.
    explicit SocketBackend(const QString &dbPath = {});
    ~SocketBackend() override;

    QVariant call(const QString &method, const QVariantList &args, QString *error) override;

private:
    bool ensureRunning(QString *error);
    QString m_dbPath;
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace tumbler::mcp
