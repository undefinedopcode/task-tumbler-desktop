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

    virtual void read(const QString &key, std::function<void(const QString &value, const QString &error)> done);
    // An empty value deletes the entry.
    virtual void write(const QString &key, const QString &value, std::function<void(const QString &error)> done);
};

// Holds nothing and never touches the keyring (screenshot mode).
class NullKeyStore : public KeyStore
{
    Q_OBJECT
public:
    using KeyStore::KeyStore;
    void read(const QString &, std::function<void(const QString &, const QString &)> done) override { done({}, {}); }
    void write(const QString &, const QString &, std::function<void(const QString &)> done) override
    {
        done(QStringLiteral("no keyring in this mode"));
    }
};

} // namespace tumbler
