#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace tumbler {

// Secrets (API keys) in the desktop keyring: KWallet on Plasma, libsecret
// elsewhere. Never in the database or config files. Calls are async.
class KeyStore : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    void read(const QString &key, std::function<void(const QString &value, const QString &error)> done);
    // An empty value deletes the entry.
    void write(const QString &key, const QString &value, std::function<void(const QString &error)> done);
};

} // namespace tumbler
