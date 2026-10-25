#include "webpush.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>
#include <memory>

namespace tumbler::remote {

namespace {

template<typename T, void (*F)(T *)>
struct Deleter {
    void operator()(T *p) const { F(p); }
};
using PKey = std::unique_ptr<EVP_PKEY, Deleter<EVP_PKEY, EVP_PKEY_free>>;
using PKeyCtx = std::unique_ptr<EVP_PKEY_CTX, Deleter<EVP_PKEY_CTX, EVP_PKEY_CTX_free>>;
using Bn = std::unique_ptr<BIGNUM, Deleter<BIGNUM, BN_free>>;
using ParamBld = std::unique_ptr<OSSL_PARAM_BLD, Deleter<OSSL_PARAM_BLD, OSSL_PARAM_BLD_free>>;
using Params = std::unique_ptr<OSSL_PARAM, Deleter<OSSL_PARAM, OSSL_PARAM_free>>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, Deleter<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>>;
using MdCtx = std::unique_ptr<EVP_MD_CTX, Deleter<EVP_MD_CTX, EVP_MD_CTX_free>>;
using EcGroup = std::unique_ptr<EC_GROUP, Deleter<EC_GROUP, EC_GROUP_free>>;
using EcPoint = std::unique_ptr<EC_POINT, Deleter<EC_POINT, EC_POINT_free>>;
using EcdsaSig = std::unique_ptr<ECDSA_SIG, Deleter<ECDSA_SIG, ECDSA_SIG_free>>;

const char *const kCurve = "prime256v1";

QByteArray publicFromPrivate(const QByteArray &priv)
{
    EcGroup group(EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1));
    Bn d(BN_bin2bn(reinterpret_cast<const unsigned char *>(priv.constData()), int(priv.size()), nullptr));
    EcPoint point(EC_POINT_new(group.get()));
    if (!group || !d || !point || !EC_POINT_mul(group.get(), point.get(), d.get(), nullptr, nullptr, nullptr))
        return {};
    QByteArray out(65, Qt::Uninitialized);
    const size_t n = EC_POINT_point2oct(group.get(), point.get(), POINT_CONVERSION_UNCOMPRESSED,
                                        reinterpret_cast<unsigned char *>(out.data()), size_t(out.size()), nullptr);
    return n == 65 ? out : QByteArray();
}

// An EVP key from raw P-256 material; private may be empty for public-only.
PKey makeKey(const QByteArray &priv, const QByteArray &pub)
{
    ParamBld bld(OSSL_PARAM_BLD_new());
    Bn d;
    if (!bld || !OSSL_PARAM_BLD_push_utf8_string(bld.get(), OSSL_PKEY_PARAM_GROUP_NAME, kCurve, 0)
        || !OSSL_PARAM_BLD_push_octet_string(bld.get(), OSSL_PKEY_PARAM_PUB_KEY, pub.constData(), size_t(pub.size())))
        return {};
    if (!priv.isEmpty()) {
        d.reset(BN_bin2bn(reinterpret_cast<const unsigned char *>(priv.constData()), int(priv.size()), nullptr));
        if (!d || !OSSL_PARAM_BLD_push_BN(bld.get(), OSSL_PKEY_PARAM_PRIV_KEY, d.get()))
            return {};
    }
    Params params(OSSL_PARAM_BLD_to_param(bld.get()));
    PKeyCtx ctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    EVP_PKEY *key = nullptr;
    if (!params || !ctx || EVP_PKEY_fromdata_init(ctx.get()) <= 0
        || EVP_PKEY_fromdata(ctx.get(), &key, priv.isEmpty() ? EVP_PKEY_PUBLIC_KEY : EVP_PKEY_KEYPAIR, params.get()) <= 0)
        return {};
    return PKey(key);
}

QByteArray hmacSha256(const QByteArray &key, const QByteArray &data)
{
    QByteArray out(32, Qt::Uninitialized);
    size_t len = 0;
    if (!EVP_Q_mac(nullptr, "HMAC", nullptr, "SHA256", nullptr, key.constData(), size_t(key.size()),
                   reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size()),
                   reinterpret_cast<unsigned char *>(out.data()), size_t(out.size()), &len))
        return {};
    return out;
}

// HKDF-SHA256 for outputs up to 32 bytes (one expand round is all Web Push needs).
QByteArray hkdf(const QByteArray &salt, const QByteArray &ikm, const QByteArray &info, int length)
{
    const QByteArray prk = hmacSha256(salt, ikm);
    return hmacSha256(prk, info + QByteArray(1, '\x01')).left(length);
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

QByteArray toBase64Url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray fromBase64Url(const QByteArray &text)
{
    return QByteArray::fromBase64(text, QByteArray::Base64UrlEncoding);
}

VapidKeys VapidKeys::generate()
{
    VapidKeys k;
    EVP_PKEY *raw = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    PKey key(raw);
    BIGNUM *d = nullptr;
    if (!key || !EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_PRIV_KEY, &d))
        return k;
    Bn dd(d);
    k.privateKey.resize(32);
    BN_bn2binpad(dd.get(), reinterpret_cast<unsigned char *>(k.privateKey.data()), 32);
    k.publicKey = publicFromPrivate(k.privateKey);
    return k;
}

QJsonObject Subscription::toJson() const
{
    return {{QStringLiteral("endpoint"), endpoint},
            {QStringLiteral("keys"), QJsonObject{{QStringLiteral("p256dh"), QString::fromLatin1(toBase64Url(p256dh))},
                                                 {QStringLiteral("auth"), QString::fromLatin1(toBase64Url(auth))}}},
            {QStringLiteral("label"), label},
            {QStringLiteral("addedAt"), addedAt}};
}

std::optional<Subscription> Subscription::fromJson(const QJsonObject &json)
{
    Subscription s;
    s.endpoint = json.value(QLatin1String("endpoint")).toString();
    const QJsonObject keys = json.value(QLatin1String("keys")).toObject();
    s.p256dh = fromBase64Url(keys.value(QLatin1String("p256dh")).toString().toLatin1());
    s.auth = fromBase64Url(keys.value(QLatin1String("auth")).toString().toLatin1());
    s.label = json.value(QLatin1String("label")).toString().left(80);
    s.addedAt = qint64(json.value(QLatin1String("addedAt")).toDouble());
    // Push services are always https; anything else is not a real subscription.
    const QUrl url(s.endpoint);
    if (url.scheme() != QLatin1String("https") || url.host().isEmpty() || s.p256dh.size() != 65 || s.auth.size() != 16)
        return std::nullopt;
    return s;
}

std::optional<QByteArray> encryptPayload(const QByteArray &plaintext, const QByteArray &uaPublic, const QByteArray &authSecret, QString *error,
                                         const QByteArray &fixedPrivate, const QByteArray &fixedSalt)
{
    if (uaPublic.size() != 65 || authSecret.size() != 16) {
        setError(error, QStringLiteral("subscription keys have the wrong size"));
        return std::nullopt;
    }
    // Ephemeral application-server key pair for this message.
    QByteArray asPrivate = fixedPrivate;
    if (asPrivate.isEmpty())
        asPrivate = VapidKeys::generate().privateKey;
    const QByteArray asPublic = publicFromPrivate(asPrivate);
    PKey mine = makeKey(asPrivate, asPublic);
    PKey theirs = makeKey({}, uaPublic);
    if (!mine || !theirs) {
        setError(error, QStringLiteral("invalid key"));
        return std::nullopt;
    }

    QByteArray shared(32, Qt::Uninitialized);
    size_t sharedLen = size_t(shared.size());
    PKeyCtx dctx(EVP_PKEY_CTX_new(mine.get(), nullptr));
    if (!dctx || EVP_PKEY_derive_init(dctx.get()) <= 0 || EVP_PKEY_derive_set_peer(dctx.get(), theirs.get()) <= 0
        || EVP_PKEY_derive(dctx.get(), reinterpret_cast<unsigned char *>(shared.data()), &sharedLen) <= 0 || sharedLen != 32) {
        setError(error, QStringLiteral("key agreement failed"));
        return std::nullopt;
    }

    QByteArray salt = fixedSalt;
    if (salt.isEmpty()) {
        salt.resize(16);
        RAND_bytes(reinterpret_cast<unsigned char *>(salt.data()), 16);
    }

    const QByteArray keyInfo = QByteArray("WebPush: info", 14) + uaPublic + asPublic; // includes the NUL
    const QByteArray ikm = hkdf(authSecret, shared, keyInfo, 32);
    const QByteArray cek = hkdf(salt, ikm, QByteArray("Content-Encoding: aes128gcm", 28), 16);
    const QByteArray nonce = hkdf(salt, ikm, QByteArray("Content-Encoding: nonce", 24), 12);

    // One record: plaintext, then the 0x02 "last record" delimiter.
    const QByteArray padded = plaintext + QByteArray(1, '\x02');
    QByteArray ciphertext(padded.size() + 16, Qt::Uninitialized);
    CipherCtx c(EVP_CIPHER_CTX_new());
    int len = 0, total = 0;
    if (!c || !EVP_EncryptInit_ex(c.get(), EVP_aes_128_gcm(), nullptr, reinterpret_cast<const unsigned char *>(cek.constData()),
                                  reinterpret_cast<const unsigned char *>(nonce.constData()))
        || !EVP_EncryptUpdate(c.get(), reinterpret_cast<unsigned char *>(ciphertext.data()), &len,
                              reinterpret_cast<const unsigned char *>(padded.constData()), int(padded.size()))) {
        setError(error, QStringLiteral("encryption failed"));
        return std::nullopt;
    }
    total = len;
    if (!EVP_EncryptFinal_ex(c.get(), reinterpret_cast<unsigned char *>(ciphertext.data()) + total, &len)
        || !EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_GET_TAG, 16, reinterpret_cast<unsigned char *>(ciphertext.data()) + total + len)) {
        setError(error, QStringLiteral("encryption failed"));
        return std::nullopt;
    }
    total += len + 16;
    ciphertext.resize(total);

    // Header: salt | record size (4096) | key id length | key id (our public key).
    QByteArray header = salt;
    header.append(QByteArray::fromHex("00001000"));
    header.append(char(asPublic.size()));
    header.append(asPublic);
    return header + ciphertext;
}

QString vapidJwt(const VapidKeys &keys, const QString &audience, const QString &subject, qint64 expires, QString *error)
{
    const QByteArray header = toBase64Url(R"({"typ":"JWT","alg":"ES256"})");
    const QByteArray claims = toBase64Url(QJsonDocument(QJsonObject{{QStringLiteral("aud"), audience},
                                                                    {QStringLiteral("exp"), expires},
                                                                    {QStringLiteral("sub"), subject}})
                                              .toJson(QJsonDocument::Compact));
    const QByteArray input = header + '.' + claims;

    PKey key = makeKey(keys.privateKey, keys.publicKey);
    MdCtx md(EVP_MD_CTX_new());
    size_t derLen = 0;
    if (!key || !md || EVP_DigestSignInit(md.get(), nullptr, EVP_sha256(), nullptr, key.get()) <= 0
        || EVP_DigestSign(md.get(), nullptr, &derLen, reinterpret_cast<const unsigned char *>(input.constData()), size_t(input.size())) <= 0) {
        setError(error, QStringLiteral("signing failed"));
        return {};
    }
    QByteArray der(qsizetype(derLen), Qt::Uninitialized);
    if (EVP_DigestSign(md.get(), reinterpret_cast<unsigned char *>(der.data()), &derLen,
                       reinterpret_cast<const unsigned char *>(input.constData()), size_t(input.size())) <= 0) {
        setError(error, QStringLiteral("signing failed"));
        return {};
    }
    // JWS wants raw r||s (64 bytes), not DER.
    const unsigned char *p = reinterpret_cast<const unsigned char *>(der.constData());
    EcdsaSig sig(d2i_ECDSA_SIG(nullptr, &p, long(derLen)));
    if (!sig) {
        setError(error, QStringLiteral("signing failed"));
        return {};
    }
    QByteArray raw(64, '\0');
    BN_bn2binpad(ECDSA_SIG_get0_r(sig.get()), reinterpret_cast<unsigned char *>(raw.data()), 32);
    BN_bn2binpad(ECDSA_SIG_get0_s(sig.get()), reinterpret_cast<unsigned char *>(raw.data()) + 32, 32);
    return QString::fromLatin1(input + '.' + toBase64Url(raw));
}

PushSender::PushSender(QNetworkAccessManager *network, VapidKeys keys, QString subject, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_keys(std::move(keys))
    , m_subject(std::move(subject))
{
}

void PushSender::send(const Subscription &sub, const QJsonObject &payload, int ttlSeconds, const QString &urgency,
                      std::function<void(int, const QString &)> done)
{
    QString error;
    const auto body = encryptPayload(QJsonDocument(payload).toJson(QJsonDocument::Compact), sub.p256dh, sub.auth, &error);
    const QUrl endpoint(sub.endpoint);
    const QString audience = endpoint.scheme() + QStringLiteral("://") + endpoint.authority();
    const QString jwt = body ? vapidJwt(m_keys, audience, m_subject, QDateTime::currentSecsSinceEpoch() + 12 * 3600, &error) : QString();
    if (!body || jwt.isEmpty()) {
        done(0, error);
        return;
    }
    QNetworkRequest req(endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    req.setRawHeader("Content-Encoding", "aes128gcm");
    req.setRawHeader("TTL", QByteArray::number(ttlSeconds));
    req.setRawHeader("Urgency", urgency.toLatin1());
    req.setRawHeader("Authorization", "vapid t=" + jwt.toLatin1() + ", k=" + toBase64Url(m_keys.publicKey));
    req.setTransferTimeout(30000);
    QNetworkReply *reply = m_network->post(req, *body);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done(status, status >= 200 && status < 300 ? QString() : reply->errorString());
    });
}

} // namespace tumbler::remote
