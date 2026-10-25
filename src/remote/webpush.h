#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace tumbler::remote {

QByteArray toBase64Url(const QByteArray &bytes);
QByteArray fromBase64Url(const QByteArray &text);

// The app server's VAPID key pair (P-256): 32-byte private scalar and
// 65-byte uncompressed public point. Generated once at pairing.
struct VapidKeys {
    QByteArray privateKey;
    QByteArray publicKey;

    static VapidKeys generate();
    bool isValid() const { return privateKey.size() == 32 && publicKey.size() == 65; }
};

// A phone's push endpoint as its browser handed it over.
struct Subscription {
    QString endpoint;
    QByteArray p256dh; // 65 bytes
    QByteArray auth;   // 16 bytes
    QString label;     // "Pixel 8", for Settings
    qint64 addedAt = 0;

    QJsonObject toJson() const;
    static std::optional<Subscription> fromJson(const QJsonObject &json);
};

// RFC 8291 message encryption: the aes128gcm body for one push. The
// ephemeral key and salt are random unless given (tests use the RFC's).
std::optional<QByteArray> encryptPayload(const QByteArray &plaintext, const QByteArray &uaPublic, const QByteArray &authSecret,
                                         QString *error, const QByteArray &fixedPrivate = {}, const QByteArray &fixedSalt = {});

// RFC 8292 VAPID: an ES256-signed JWT for the push service at `audience`
// (scheme://host of the endpoint), valid until `expires` (Unix seconds).
QString vapidJwt(const VapidKeys &keys, const QString &audience, const QString &subject, qint64 expires, QString *error);

// Sends pushes. 404/410 means the subscription is gone and should be forgotten.
class PushSender : public QObject
{
    Q_OBJECT
public:
    PushSender(QNetworkAccessManager *network, VapidKeys keys, QString subject, QObject *parent = nullptr);

    // urgency: very-low | low | normal | high
    void send(const Subscription &subscription, const QJsonObject &payload, int ttlSeconds, const QString &urgency,
              std::function<void(int status, const QString &error)> done);

private:
    QNetworkAccessManager *m_network;
    VapidKeys m_keys;
    QString m_subject;
};

} // namespace tumbler::remote
