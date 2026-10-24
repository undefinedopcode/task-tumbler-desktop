#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <memory>

// A one-connection-at-a-time HTTP server that records requests and answers
// from a queue. Enough to exercise the providers without the real APIs.
class FakeServer : public QObject
{
public:
    struct Seen {
        QString path;
        QHash<QString, QString> headers;
        QJsonObject body;
    };

    FakeServer()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                connect(s, &QTcpSocket::readyRead, s, [this, s, buffer] {
                    buffer->append(s->readAll());
                    const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                    Seen seen;
                    seen.path = QString::fromLatin1(lines.first().split(' ').value(1));
                    qsizetype length = 0;
                    for (const QByteArray &l : lines.mid(1)) {
                        const qsizetype colon = l.indexOf(':');
                        const QString key = QString::fromLatin1(l.left(colon)).trimmed().toLower();
                        const QString value = QString::fromLatin1(l.mid(colon + 1)).trimmed();
                        seen.headers.insert(key, value);
                        if (key == QLatin1String("content-length"))
                            length = value.toLongLong();
                    }
                    if (buffer->size() - headerEnd - 4 < length)
                        return;
                    seen.body = QJsonDocument::fromJson(buffer->mid(headerEnd + 4, length)).object();
                    requests.append(seen);
                    const auto [status, body] = replies.isEmpty() ? qMakePair(500, QByteArray("{}")) : replies.takeFirst();
                    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
            }
        });
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())); }
    void reply(int status, const QJsonObject &body) { replies.append({status, QJsonDocument(body).toJson()}); }

    QTcpServer server;
    QList<QPair<int, QByteArray>> replies;
    QList<Seen> requests;
};
