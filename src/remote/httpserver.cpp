#include "httpserver.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <openssl/crypto.h>

namespace tumbler::remote {

namespace {
constexpr qsizetype kMaxHeader = 16 * 1024;
constexpr qsizetype kMaxBody = 64 * 1024;
constexpr qint64 kFailWindowMs = 60 * 1000;
constexpr int kFailLimit = 3;
constexpr qint64 kBlockMs = 10 * 60 * 1000;

QByteArray reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 429: return "Too Many Requests";
    default: return "Error";
    }
}

QByteArray mimeFor(const QString &path)
{
    if (path.endsWith(QLatin1String(".html"))) return "text/html; charset=utf-8";
    if (path.endsWith(QLatin1String(".js"))) return "text/javascript; charset=utf-8";
    if (path.endsWith(QLatin1String(".css"))) return "text/css; charset=utf-8";
    if (path.endsWith(QLatin1String(".svg"))) return "image/svg+xml";
    if (path.endsWith(QLatin1String(".png"))) return "image/png";
    if (path.endsWith(QLatin1String(".webmanifest"))) return "application/manifest+json";
    return "application/octet-stream";
}

QString hostOnly(const QByteArray &hostPort)
{
    // "[::1]:7789", "example.ts.net:7789", "127.0.0.1"
    QString h = QString::fromLatin1(hostPort).trimmed();
    if (h.startsWith(QLatin1Char('['))) return h.mid(1, h.indexOf(QLatin1Char(']')) - 1).toLower();
    const qsizetype colon = h.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && h.count(QLatin1Char(':')) == 1) h = h.left(colon);
    return h.toLower();
}
} // namespace

HttpServer::HttpServer(Backend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *s = m_server->nextPendingConnection()) {
            m_buffers.insert(s, {});
            connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
            connect(s, &QTcpSocket::disconnected, this, [this, s] {
                m_buffers.remove(s);
                const qsizetype before = m_streams.size();
                m_streams.removeIf([s](const QPointer<QTcpSocket> &p) { return !p || p == s; });
                if (m_streams.size() != before)
                    Q_EMIT streamsChanged(int(m_streams.size()));
                s->deleteLater();
            });
            // Half-sent requests don't get to hold a socket forever.
            QTimer::singleShot(15000, s, [this, s] {
                if (m_buffers.contains(s) && !m_streams.contains(s))
                    s->abort();
            });
        }
    });
    // Comments keep proxies (tailscale serve) from closing idle streams.
    m_keepAlive.setInterval(25000);
    connect(&m_keepAlive, &QTimer::timeout, this, [this] {
        for (const auto &s : std::as_const(m_streams)) {
            if (s)
                s->write(": keep-alive\n\n");
        }
    });
    m_keepAlive.start();
}

HttpServer::~HttpServer() = default;

bool HttpServer::isTailnet(const QHostAddress &a)
{
    return a.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10) || a.isInSubnet(QHostAddress(QStringLiteral("fd7a:115c:a1e0::")), 48);
}

bool HttpServer::bindAllowed(const QHostAddress &address, bool allowInsecure, QString *why)
{
    if (address.isLoopback() || isTailnet(address) || allowInsecure)
        return true;
    if (why)
        *why = address == QHostAddress::Any || address == QHostAddress::AnyIPv6 || address == QHostAddress::AnyIPv4
            ? QStringLiteral("listening on every interface would send the pairing token in the clear on your local network")
            : QStringLiteral("that address is on an ordinary network; listen on 127.0.0.1 behind tailscale serve, or on your tailnet address");
    return false;
}

bool HttpServer::listen(const QHostAddress &address, quint16 port, bool allowInsecure, QString *error)
{
    if (!bindAllowed(address, allowInsecure, error))
        return false;
    if (m_token.size() < 16) {
        if (error)
            *error = QStringLiteral("no pairing token");
        return false;
    }
    if (!m_server->listen(address, port)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    return true;
}

void HttpServer::close()
{
    m_server->close();
    for (const auto &s : std::as_const(m_streams)) {
        if (s)
            s->disconnectFromHost();
    }
    m_streams.clear();
}

bool HttpServer::isListening() const
{
    return m_server->isListening();
}

quint16 HttpServer::port() const
{
    return m_server->serverPort();
}

void HttpServer::onReadyRead(QTcpSocket *socket)
{
    if (m_streams.contains(socket))
        return; // streams only send
    QByteArray &buf = m_buffers[socket];
    buf.append(socket->readAll());

    const qsizetype headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (buf.size() > kMaxHeader)
            respond(socket, 413, "text/plain", "headers too large");
        return;
    }
    Request req;
    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
    if (start.size() < 3) {
        respond(socket, 400, "text/plain", "bad request");
        return;
    }
    req.method = start.at(0);
    req.path = start.at(1);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            req.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    const qsizetype length = req.headers.value("content-length", "0").toLongLong();
    if (length > kMaxBody || length < 0) {
        respond(socket, 413, "text/plain", "too large");
        return;
    }
    if (buf.size() - headerEnd - 4 < length)
        return; // wait for the rest
    req.body = buf.mid(headerEnd + 4, length);
    buf.clear();
    handle(socket, req);
}

QString HttpServer::clientKey(QTcpSocket *socket, const Request &req) const
{
    // Behind tailscale serve every request comes from loopback; it says who
    // the real client is.
    if (socket->peerAddress().isLoopback() && req.headers.contains("x-forwarded-for"))
        return QString::fromLatin1(req.headers.value("x-forwarded-for").split(',').first().trimmed());
    return socket->peerAddress().toString();
}

bool HttpServer::authorized(const Request &req) const
{
    const QByteArray header = req.headers.value("authorization");
    if (!header.startsWith("Bearer "))
        return false;
    const QByteArray given = header.mid(7).trimmed();
    return given.size() == m_token.size() && CRYPTO_memcmp(given.constData(), m_token.constData(), size_t(m_token.size())) == 0;
}

bool HttpServer::blocked(const QString &client)
{
    return m_failures.value(client).blockedUntil > QDateTime::currentMSecsSinceEpoch();
}

void HttpServer::recordFailure(const QString &client)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Failures &f = m_failures[client];
    if (now - f.windowStart > kFailWindowMs) {
        f.windowStart = now;
        f.count = 0;
    }
    if (++f.count >= kFailLimit)
        f.blockedUntil = now + kBlockMs;
}

void HttpServer::handle(QTcpSocket *socket, const Request &req)
{
    const QByteArray path = QUrl::fromPercentEncoding(req.path.split('?').first()).toUtf8();

    if (!path.startsWith("/api/")) {
        if (req.method != "GET" && req.method != "HEAD") {
            respond(socket, 405, "text/plain", "method not allowed");
            return;
        }
        serveStatic(socket, path);
        return;
    }

    const QString client = clientKey(socket, req);
    if (blocked(client)) {
        respondJson(socket, 429, {{QStringLiteral("error"), QStringLiteral("too many wrong tokens; try again later")}});
        return;
    }
    if (!authorized(req)) {
        recordFailure(client);
        respondJson(socket, 401, {{QStringLiteral("error"), QStringLiteral("not paired")}});
        return;
    }
    // Writes must come from a page served here (or from no browser at all).
    if (req.method != "GET" && req.headers.contains("origin")) {
        const QUrl origin(QString::fromLatin1(req.headers.value("origin")));
        if (origin.host().toLower() != hostOnly(req.headers.value("host"))) {
            respondJson(socket, 403, {{QStringLiteral("error"), QStringLiteral("cross-origin request refused")}});
            return;
        }
    }

    const QJsonObject body = QJsonDocument::fromJson(req.body).object();
    if (path == "/api/snapshot" && req.method == "GET") {
        respondJson(socket, 200, m_backend->snapshot());
    } else if (path == "/api/closeout" && req.method == "GET") {
        respondJson(socket, 200, m_backend->closeOut());
    } else if (path == "/api/events" && req.method == "GET") {
        openStream(socket);
    } else if (path == "/api/command" && req.method == "POST") {
        QString error;
        const QJsonObject result = m_backend->command(body, &error);
        if (!error.isEmpty())
            respondJson(socket, 400, {{QStringLiteral("error"), error}});
        else
            respondJson(socket, 200, result);
    } else if (path == "/api/preview" && req.method == "POST") {
        respondJson(socket, 200, m_backend->preview(body.value(QLatin1String("text")).toString().left(500)));
    } else if (path == "/api/vapid" && req.method == "GET") {
        respondJson(socket, 200, {{QStringLiteral("publicKey"), QString::fromLatin1(toBase64Url(m_backend->vapidPublicKey()))}});
    } else if (path == "/api/push" && req.method == "POST") {
        const auto sub = Subscription::fromJson(body);
        if (!sub)
            respondJson(socket, 400, {{QStringLiteral("error"), QStringLiteral("not a valid push subscription")}});
        else
            respondJson(socket, 200, {{QStringLiteral("ok"), m_backend->addSubscription(*sub)}});
    } else if (path == "/api/push" && req.method == "DELETE") {
        respondJson(socket, 200, {{QStringLiteral("ok"), m_backend->removeSubscription(body.value(QLatin1String("endpoint")).toString())}});
    } else {
        respondJson(socket, 404, {{QStringLiteral("error"), QStringLiteral("no such endpoint")}});
    }
}

void HttpServer::serveStatic(QTcpSocket *socket, const QByteArray &rawPath)
{
    QString path = QString::fromUtf8(rawPath);
    if (path == QLatin1String("/") || path.isEmpty())
        path = QStringLiteral("/index.html");
    // No climbing out of the web root.
    if (path.contains(QLatin1String("..")) || path.contains(QLatin1Char('\\'))) {
        respond(socket, 404, "text/plain", "not found");
        return;
    }
    QFile f(m_staticRoot + path);
    if (!f.open(QIODevice::ReadOnly) || QFileInfo(f).isDir()) {
        respond(socket, 404, "text/plain", "not found");
        return;
    }
    QList<QPair<QByteArray, QByteArray>> extra;
    // The service worker controls the whole site.
    if (path == QLatin1String("/sw.js"))
        extra.append(qMakePair(QByteArray("Service-Worker-Allowed"), QByteArray("/")));
    respond(socket, 200, mimeFor(path), f.readAll(), extra);
}

void HttpServer::openStream(QTcpSocket *socket)
{
    socket->write("HTTP/1.1 200 OK\r\n"
                  "Content-Type: text/event-stream\r\n"
                  "Cache-Control: no-store\r\n"
                  "X-Accel-Buffering: no\r\n"
                  "Connection: keep-alive\r\n\r\n"
                  "event: hello\ndata: {}\n\n");
    m_buffers.remove(socket);
    m_streams.append(socket);
    Q_EMIT streamsChanged(int(m_streams.size()));
}

void HttpServer::broadcast(const QJsonObject &event)
{
    const QByteArray data = "event: " + event.value(QLatin1String("type")).toString(QStringLiteral("changed")).toUtf8() + "\ndata: "
        + QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n\n";
    for (const auto &s : std::as_const(m_streams)) {
        if (s)
            s->write(data);
    }
}

void HttpServer::respondJson(QTcpSocket *socket, int status, const QJsonObject &body)
{
    respond(socket, status, "application/json", QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void HttpServer::respond(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body,
                         const QList<QPair<QByteArray, QByteArray>> &extraHeaders)
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason(status) + "\r\n";
    head += "Content-Type: " + contentType + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "X-Content-Type-Options: nosniff\r\n";
    head += "Referrer-Policy: no-referrer\r\n";
    head += "Content-Security-Policy: default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; "
            "connect-src 'self'; manifest-src 'self'; worker-src 'self'; frame-ancestors 'none'\r\n";
    for (const auto &[k, v] : extraHeaders)
        head += k + ": " + v + "\r\n";
    head += "Connection: close\r\n\r\n";
    socket->write(head + body);
    socket->disconnectFromHost();
    m_buffers.remove(socket);
}

} // namespace tumbler::remote
