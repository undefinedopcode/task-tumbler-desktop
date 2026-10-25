#include "httpserver.h"
#include "webpush.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTest>

#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

using namespace tumbler::remote;

namespace {

QByteArray u(const char *b64url)
{
    return fromBase64Url(QByteArray(b64url));
}

class FakeBackend : public Backend
{
public:
    QJsonObject snapshot() override { return {{"today", QJsonObject{{"date", "2026-10-25"}}}}; }
    QJsonObject command(const QJsonObject &c, QString *error) override
    {
        commands.append(c);
        if (c.value("type") == "explode")
            *error = "no such command";
        return {{"ok", true}};
    }
    QJsonObject preview(const QString &text) override { return {{"title", text.toUpper()}}; }
    QByteArray vapidPublicKey() override { return QByteArray(65, '\x04'); }
    bool addSubscription(const Subscription &s) override
    {
        subscriptions.append(s);
        return true;
    }
    bool removeSubscription(const QString &endpoint) override { return subscriptions.removeIf([&](const Subscription &s) { return s.endpoint == endpoint; }) > 0; }

    QList<QJsonObject> commands;
    QList<Subscription> subscriptions;
};

const QByteArray kToken = "0123456789abcdef0123456789abcdef";

struct Response {
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

Response http(quint16 port, const QByteArray &method, const QByteArray &path, const QByteArray &body = {},
              const QList<QPair<QByteArray, QByteArray>> &headers = {}, bool withToken = true)
{
    // The server runs on this thread, so wait by spinning the event loop,
    // never with the blocking waitFor* calls.
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    if (!QTest::qWaitFor([&] { return s.state() == QAbstractSocket::ConnectedState; }, 2000))
        return {};
    QByteArray req = method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\n";
    if (withToken)
        req += "Authorization: Bearer " + kToken + "\r\n";
    for (const auto &[k, v] : headers)
        req += k + ": " + v + "\r\n";
    req += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    s.write(req);
    QByteArray raw;
    // The server closes the connection after each response.
    (void)QTest::qWaitFor([&] {
        raw += s.readAll();
        return s.state() == QAbstractSocket::UnconnectedState;
    }, 3000);
    raw += s.readAll();
    Response r;
    const qsizetype end = raw.indexOf("\r\n\r\n");
    const QList<QByteArray> lines = raw.left(end).split('\n');
    r.status = lines.value(0).split(' ').value(1).toInt();
    for (const QByteArray &l : lines.mid(1)) {
        const qsizetype c = l.indexOf(':');
        if (c > 0)
            r.headers.insert(l.left(c).trimmed().toLower(), l.mid(c + 1).trimmed());
    }
    r.body = raw.mid(end + 4);
    return r;
}

// Verifies an ES256 JWS signature (raw r||s) against a raw P-256 public key.
bool verifyEs256(const QByteArray &signingInput, const QByteArray &rawSig, const QByteArray &pub)
{
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "prime256v1", 0);
    OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub.constData(), size_t(pub.size()));
    OSSL_PARAM *params = OSSL_PARAM_BLD_to_param(bld);
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
    EVP_PKEY *key = nullptr;
    EVP_PKEY_fromdata_init(ctx);
    EVP_PKEY_fromdata(ctx, &key, EVP_PKEY_PUBLIC_KEY, params);

    ECDSA_SIG *sig = ECDSA_SIG_new();
    ECDSA_SIG_set0(sig, BN_bin2bn(reinterpret_cast<const unsigned char *>(rawSig.constData()), 32, nullptr),
                   BN_bin2bn(reinterpret_cast<const unsigned char *>(rawSig.constData()) + 32, 32, nullptr));
    unsigned char *der = nullptr;
    const int derLen = i2d_ECDSA_SIG(sig, &der);

    EVP_MD_CTX *md = EVP_MD_CTX_new();
    const bool ok = EVP_DigestVerifyInit(md, nullptr, EVP_sha256(), nullptr, key) == 1
        && EVP_DigestVerify(md, der, size_t(derLen), reinterpret_cast<const unsigned char *>(signingInput.constData()), size_t(signingInput.size())) == 1;

    EVP_MD_CTX_free(md);
    OPENSSL_free(der);
    ECDSA_SIG_free(sig);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);
    OSSL_PARAM_free(params);
    OSSL_PARAM_BLD_free(bld);
    return ok;
}

} // namespace

class TestRemote : public QObject
{
    Q_OBJECT

private:
    FakeBackend m_backend;
    std::unique_ptr<HttpServer> m_server;
    quint16 port() const { return m_server->port(); }

private slots:
    // ---- Web Push ----------------------------------------------------------

    // RFC 8291 Appendix A, checked independently in Python before use.
    void rfc8291Vector()
    {
        QString error;
        const auto body = encryptPayload(u("V2hlbiBJIGdyb3cgdXAsIEkgd2FudCB0byBiZSBhIHdhdGVybWVsb24"),
                                         u("BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4"),
                                         u("BTBZMqHH6r4Tts7J_aSIgg"), &error, u("yfWPiYE-n46HLnH0KqZOF1fJJU3MYrct3AELtAQ-oRw"),
                                         u("DGv6ra1nlYgDCS1FRnbzlw"));
        QVERIFY2(body, qPrintable(error));
        QCOMPARE(toBase64Url(*body),
                 QByteArray("DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vCYLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A_"
                            "yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXPXLWyouBWLVWGNWQexSgSxsj_Qulcy4a-fN"));
    }

    void randomEncryptionShape()
    {
        const QByteArray ua = u("BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4");
        QString error;
        const auto a = encryptPayload("hello", ua, QByteArray(16, 'x'), &error);
        const auto b = encryptPayload("hello", ua, QByteArray(16, 'x'), &error);
        QVERIFY(a && b);
        QVERIFY(*a != *b); // fresh salt and key every time
        QCOMPARE(a->size(), 16 + 4 + 1 + 65 + 5 + 1 + 16);
        QCOMPARE(a->mid(16, 5), QByteArray::fromHex("0000100041"));
        QVERIFY(!encryptPayload("x", QByteArray(10, 'x'), QByteArray(16, 'x'), &error));
    }

    void vapid()
    {
        const VapidKeys keys = VapidKeys::generate();
        QVERIFY(keys.isValid());
        QCOMPARE(keys.publicKey.at(0), '\x04');
        QString error;
        const QString jwt = vapidJwt(keys, "https://fcm.googleapis.com", "mailto:me@example.com", 1800000000, &error);
        const QStringList parts = jwt.split('.');
        QCOMPARE(parts.size(), 3);
        const QJsonObject header = QJsonDocument::fromJson(fromBase64Url(parts[0].toLatin1())).object();
        const QJsonObject claims = QJsonDocument::fromJson(fromBase64Url(parts[1].toLatin1())).object();
        QCOMPARE(header.value("alg").toString(), QStringLiteral("ES256"));
        QCOMPARE(claims.value("aud").toString(), QStringLiteral("https://fcm.googleapis.com"));
        QCOMPARE(claims.value("exp").toInteger(), 1800000000);
        const QByteArray sig = fromBase64Url(parts[2].toLatin1());
        QCOMPARE(sig.size(), 64);
        QVERIFY(verifyEs256((parts[0] + '.' + parts[1]).toLatin1(), sig, keys.publicKey));
        QVERIFY(!verifyEs256((parts[0] + ".tampered").toLatin1(), sig, keys.publicKey));
    }

    void subscriptionValidation()
    {
        const QJsonObject good{{"endpoint", "https://fcm.googleapis.com/fcm/send/abc"},
                               {"keys", QJsonObject{{"p256dh", QString::fromLatin1(toBase64Url(QByteArray(65, '\x04')))},
                                                    {"auth", QString::fromLatin1(toBase64Url(QByteArray(16, 'a')))}}}};
        QVERIFY(Subscription::fromJson(good));
        QJsonObject http = good;
        http["endpoint"] = "http://evil.example/push";
        QVERIFY(!Subscription::fromJson(http));
        QJsonObject shortKey = good;
        shortKey["keys"] = QJsonObject{{"p256dh", "AAAA"}, {"auth", "AAAA"}};
        QVERIFY(!Subscription::fromJson(shortKey));
        QCOMPARE(Subscription::fromJson(Subscription::fromJson(good)->toJson())->endpoint, good["endpoint"].toString());
    }

    // ---- HTTP server -------------------------------------------------------

    void init()
    {
        m_backend = FakeBackend();
        m_server = std::make_unique<HttpServer>(&m_backend);
        m_server->setToken(QString::fromLatin1(kToken));
        QString error;
        QVERIFY2(m_server->listen(QHostAddress::LocalHost, 0, false, &error), qPrintable(error));
    }

    void cleanup() { m_server.reset(); }

    void bindPolicy()
    {
        QString why;
        QVERIFY(HttpServer::bindAllowed(QHostAddress::LocalHost, false, &why));
        QVERIFY(HttpServer::bindAllowed(QHostAddress::LocalHostIPv6, false, &why));
        QVERIFY(HttpServer::bindAllowed(QHostAddress("100.101.102.103"), false, &why));
        QVERIFY(HttpServer::bindAllowed(QHostAddress("fd7a:115c:a1e0::1"), false, &why));
        QVERIFY(!HttpServer::bindAllowed(QHostAddress::Any, false, &why));
        QVERIFY(why.contains("clear"));
        QVERIFY(!HttpServer::bindAllowed(QHostAddress("192.168.1.20"), false, &why));
        QVERIFY(HttpServer::bindAllowed(QHostAddress("192.168.1.20"), true, &why));

        HttpServer noToken(&m_backend);
        QString error;
        QVERIFY(!noToken.listen(QHostAddress::LocalHost, 0, false, &error));
    }

    void staticFilesNeedNoToken()
    {
        const Response index = http(port(), "GET", "/", {}, {}, false);
        QCOMPARE(index.status, 200);
        QVERIFY(index.body.contains("Task Tumbler"));
        QVERIFY(index.headers.value("content-security-policy").contains("default-src 'self'"));
        QCOMPARE(http(port(), "GET", "/sw.js", {}, {}, false).headers.value("service-worker-allowed"), QByteArray("/"));
        QCOMPARE(http(port(), "GET", "/app.js", {}, {}, false).headers.value("content-type"), QByteArray("text/javascript; charset=utf-8"));
        QCOMPARE(http(port(), "GET", "/../../etc/passwd", {}, {}, false).status, 404);
        QCOMPARE(http(port(), "GET", "/%2e%2e/secret", {}, {}, false).status, 404);
        QCOMPARE(http(port(), "POST", "/index.html", "x", {}, false).status, 405);
    }

    void apiNeedsTheToken()
    {
        QCOMPARE(http(port(), "GET", "/api/snapshot", {}, {}, false).status, 401);
        QCOMPARE(http(port(), "GET", "/api/snapshot", {}, {{"Authorization", "Bearer wrong"}}, false).status, 401);
        const Response ok = http(port(), "GET", "/api/snapshot");
        QCOMPARE(ok.status, 200);
        QCOMPARE(QJsonDocument::fromJson(ok.body).object().value("today").toObject().value("date").toString(), QStringLiteral("2026-10-25"));
        QCOMPARE(ok.headers.value("cache-control"), QByteArray("no-store"));
    }

    void wrongTokensGetBlocked()
    {
        for (int i = 0; i < 3; ++i)
            QCOMPARE(http(port(), "GET", "/api/snapshot", {}, {{"Authorization", "Bearer nope"}}, false).status, 401);
        // Blocked now, even with the right token, for this client.
        QCOMPARE(http(port(), "GET", "/api/snapshot").status, 429);
        // Behind tailscale serve, clients are told apart by X-Forwarded-For.
        QCOMPARE(http(port(), "GET", "/api/snapshot", {}, {{"X-Forwarded-For", "100.90.1.2"}}).status, 200);
    }

    void commandsPreviewAndOrigin()
    {
        Response r = http(port(), "POST", "/api/command", "{\"type\":\"capture\",\"text\":\"milk\",\"clientId\":\"c1\"}");
        QCOMPARE(r.status, 200);
        QCOMPARE(m_backend.commands.last().value("text").toString(), QStringLiteral("milk"));
        QCOMPARE(http(port(), "POST", "/api/command", "{\"type\":\"explode\"}").status, 400);

        r = http(port(), "POST", "/api/preview", "{\"text\":\"call mum\"}");
        QCOMPARE(QJsonDocument::fromJson(r.body).object().value("title").toString(), QStringLiteral("CALL MUM"));

        // A page from somewhere else may not drive the app.
        QCOMPARE(http(port(), "POST", "/api/command", "{\"type\":\"stopWork\"}", {{"Origin", "https://evil.example"}}).status, 403);
        QCOMPARE(http(port(), "POST", "/api/command", "{\"type\":\"stopWork\"}", {{"Origin", "http://127.0.0.1:1234"}}).status, 200);
        QCOMPARE(http(port(), "GET", "/api/nope").status, 404);
    }

    void oversizedBodiesRefused()
    {
        QCOMPARE(http(port(), "POST", "/api/command", QByteArray(70 * 1024, 'x')).status, 413);
    }

    void pushSubscriptions()
    {
        QCOMPARE(QJsonDocument::fromJson(http(port(), "GET", "/api/vapid").body).object().value("publicKey").toString().size(), 87);
        const QByteArray sub = QJsonDocument(QJsonObject{
                                                 {"endpoint", "https://fcm.googleapis.com/fcm/send/abc"},
                                                 {"keys", QJsonObject{{"p256dh", QString::fromLatin1(toBase64Url(QByteArray(65, '\x04')))},
                                                                      {"auth", QString::fromLatin1(toBase64Url(QByteArray(16, 'a')))}}},
                                                 {"label", "Pixel"}})
                                   .toJson();
        QCOMPARE(http(port(), "POST", "/api/push", sub).status, 200);
        QCOMPARE(m_backend.subscriptions.size(), 1);
        QCOMPARE(m_backend.subscriptions.first().label, QStringLiteral("Pixel"));
        QCOMPARE(http(port(), "POST", "/api/push", "{\"endpoint\":\"http://x\"}").status, 400);
        QCOMPARE(http(port(), "DELETE", "/api/push", "{\"endpoint\":\"https://fcm.googleapis.com/fcm/send/abc\"}").status, 200);
        QVERIFY(m_backend.subscriptions.isEmpty());
    }

    void eventStream()
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port());
        QTRY_COMPARE(s.state(), QAbstractSocket::ConnectedState);
        s.write("GET /api/events HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer " + kToken + "\r\n\r\n");
        QByteArray got;
        QTRY_VERIFY((got += s.readAll()).contains("event: hello"));
        QVERIFY(got.contains("text/event-stream"));
        QTRY_COMPARE(m_server->streamCount(), 1);

        m_server->broadcast({{"type", "changed"}, {"tables", QJsonArray{"tasks"}}});
        QTRY_VERIFY((got += s.readAll()).contains("event: changed\ndata: {\"tables\":[\"tasks\"],\"type\":\"changed\"}"));

        s.disconnectFromHost();
        QTRY_COMPARE(m_server->streamCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestRemote)
#include "tst_remote.moc"
