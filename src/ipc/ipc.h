#pragma once

#include <QJsonArray>
#include <QObject>
#include <QStringList>
#include <QVariant>
#include <functional>

class QLocalServer;
class QLocalSocket;

namespace tumbler::ipc {

// The running app listens on a local socket (a Unix socket in the user's
// runtime directory, private to them). Each request and reply is one line
// of JSON:
//   {"method": "Capture", "args": ["call mum tomorrow"]}
//   {"ok": true, "result": "task-123"}   or   {"ok": false, "error": "…"}
// Methods are the target object's scriptable slots, called by name, as
// D-Bus would. "Activate" is the server's own: a second launch handing over
// its command line.

// Where the instance using `dbPath` listens; empty for the default database.
// A test instance (--db) gets its own, so it's never mistaken for the real one.
QString socketPath(const QString &dbPath = {});

class Server : public QObject
{
    Q_OBJECT

public:
    // `target`'s Q_SCRIPTABLE slots are what callers can reach. A call can
    // fail (e.g. "not allowed") through `takeError`, which returns the last
    // error and clears it.
    Server(QObject *target, std::function<QString()> takeError, QObject *parent = nullptr);
    ~Server() override;

    // Takes over a stale socket left by a crash, but not a live one.
    bool listen(const QString &path, QString *error = nullptr);
    QString path() const;

    // The JSON reply to one request line.
    QByteArray handle(const QByteArray &line);

Q_SIGNALS:
    void activateRequested(const QStringList &arguments);

private:
    void onConnection();
    QObject *m_target;
    std::function<QString()> m_takeError;
    QLocalServer *m_server;
};

// A blocking client, for processes without an event loop of their own.
class Client
{
public:
    explicit Client(const QString &path);
    ~Client();

    bool connect(int timeoutMs = 1000);
    // The method's result (a string or a bool), or an invalid QVariant with
    // `error` set.
    QVariant call(const QString &method, const QVariantList &args, QString *error, int timeoutMs = 30000);

private:
    QString m_path;
    QLocalSocket *m_socket;
};

} // namespace tumbler::ipc
