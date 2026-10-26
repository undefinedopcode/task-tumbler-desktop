#pragma once

#include "webpush.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

class QTcpServer;
class QTcpSocket;

namespace tumbler::remote {

// What the phone can see and do. Implemented in the app by RemoteHub over
// AppController; tests use a fake.
class Backend
{
public:
    virtual ~Backend() = default;
    virtual QJsonObject snapshot() = 0;
    // The day so far, for closing it out (AppController::closeOut()).
    virtual QJsonObject closeOut() = 0;
    // {"type": "...", ...}; returns a result object, or sets error.
    virtual QJsonObject command(const QJsonObject &command, QString *error) = 0;
    virtual QJsonObject preview(const QString &text) = 0;
    virtual QByteArray vapidPublicKey() = 0;
    virtual bool addSubscription(const Subscription &subscription) = 0;
    virtual bool removeSubscription(const QString &endpoint) = 0;
};

// The phone's door into the app: static files for the web app, a JSON API,
// and a live event stream. Plain HTTP; HTTPS comes from `tailscale serve`
// in front of it.
//
//   GET  /, /app.js, ...    the web app (no data in it, so no auth)
//   GET  /api/snapshot      today, the running timer, energy
//   GET  /api/closeout      what got done, what's left, tomorrow
//   GET  /api/events        a stream of "changed" events (text/event-stream)
//   POST /api/command       {type, ...}: capture, done, bump, start, ...
//   POST /api/preview       {text}: what quick capture would understand
//   GET  /api/vapid         the push public key
//   POST /api/push          a push subscription to remember
//   DELETE /api/push        {endpoint}: forget one
//
// Every /api call needs "Authorization: Bearer <token>".
class HttpServer : public QObject
{
    Q_OBJECT
public:
    HttpServer(Backend *backend, QObject *parent = nullptr);
    ~HttpServer() override;

    void setToken(const QString &token) { m_token = token.toLatin1(); }
    // Where the web app's files are (a directory or a :/ resource path).
    void setStaticRoot(const QString &root) { m_staticRoot = root; }

    bool listen(const QHostAddress &address, quint16 port, bool allowInsecure, QString *error);
    void close();
    bool isListening() const;
    quint16 port() const;
    int streamCount() const { return int(m_streams.size()); }

    // Tells every connected phone something changed.
    void broadcast(const QJsonObject &event);

    // Loopback and tailnet addresses are always allowed (WireGuard encrypts
    // the tailnet); anything else would send the token in the clear.
    static bool bindAllowed(const QHostAddress &address, bool allowInsecure, QString *why);
    static bool isTailnet(const QHostAddress &address);

Q_SIGNALS:
    void streamsChanged(int count);

private:
    struct Request {
        QByteArray method;
        QByteArray path;
        QHash<QByteArray, QByteArray> headers; // lower-case names
        QByteArray body;
    };

    void onReadyRead(QTcpSocket *socket);
    void handle(QTcpSocket *socket, const Request &req);
    void respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
                 const QList<QPair<QByteArray, QByteArray>> &extraHeaders = {});
    void respondJson(QTcpSocket *socket, int status, const QJsonObject &body);
    void serveStatic(QTcpSocket *socket, const QByteArray &path);
    void openStream(QTcpSocket *socket);
    QString clientKey(QTcpSocket *socket, const Request &req) const;
    bool authorized(const Request &req) const;
    bool blocked(const QString &client);
    void recordFailure(const QString &client);

    Backend *m_backend;
    QTcpServer *m_server;
    QByteArray m_token;
    QString m_staticRoot = QStringLiteral(":/remote/web");
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QList<QPointer<QTcpSocket>> m_streams;
    QTimer m_keepAlive;

    struct Failures {
        int count = 0;
        qint64 windowStart = 0;
        qint64 blockedUntil = 0;
    };
    QHash<QString, Failures> m_failures;
};

} // namespace tumbler::remote
