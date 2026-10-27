#pragma once

#include "httpserver.h"
#include "webpush.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <memory>

namespace tumbler {

class AppController;

// The phone companion (docs §6a, the Llamington pattern): serves the phone
// web app and its API on 127.0.0.1, for `tailscale serve` to put on the
// tailnet over HTTPS; pairs phones by QR code; sends Web Push.
//
// Secrets (pairing token, VAPID keys, phones' push subscriptions, recently
// seen capture ids) live in a 0600 JSON file; switches and addresses are
// ordinary settings.
class RemoteHub : public QObject, public remote::Backend
{
    Q_OBJECT

    // {enabled, running, error, listen, publicUrl, pairUrl, phones: [{endpoint, label, addedAt}],
    //  streams, serveCommand, notify, tailscaleName}
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

public:
    RemoteHub(AppController *app, const QString &secretsPath, QObject *parent = nullptr);
    ~RemoteHub() override;

    static void setInstance(RemoteHub *hub);
    static QString defaultSecretsPath();

    QVariantMap state() const;
    bool hasPhones() const { return !m_subscriptions.isEmpty(); }
    quint16 port() const;

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void setPublicUrl(const QString &url);
    // New token: every paired phone has to scan again.
    Q_INVOKABLE void rotateToken();
    Q_INVOKABLE void forgetPhone(const QString &endpoint);
    Q_INVOKABLE void detectTailscale();
    Q_INVOKABLE void sendTest();
    Q_INVOKABLE void copyToClipboard(const QString &text);

    // A notification for every paired phone. `payload` follows the service
    // worker's shape: {title, body, tag, actions: [{action, title}], commands: {action: command}}.
    void push(const QJsonObject &payload, const QString &urgency = QStringLiteral("high"));

    // Where a reminder goes: "desktop", "phone" or "both". mode is the
    // remote_notify setting: "away" (default), "both" or "desktop".
    static QString route(const QString &mode, bool phonePaired, bool userAway);

    // remote::Backend
    QJsonObject snapshot() override;
    QJsonObject closeOut() override;
    QJsonObject command(const QJsonObject &command, QString *error) override;
    QJsonObject preview(const QString &text) override;
    QByteArray vapidPublicKey() override { return m_vapid.publicKey; }
    bool addSubscription(const remote::Subscription &subscription) override;
    bool removeSubscription(const QString &endpoint) override;

Q_SIGNALS:
    void stateChanged();

private:
    void loadSecrets();
    void saveSecrets();
    void configure();
    QString origin() const;

    AppController *m_app;
    QString m_secretsPath;
    remote::HttpServer m_server;
    QNetworkAccessManager m_network;
    std::unique_ptr<remote::PushSender> m_sender;
    QTimer m_changed;

    QString m_token;
    remote::VapidKeys m_vapid;
    QList<remote::Subscription> m_subscriptions;
    QStringList m_seenCaptures;

    QString m_error;
    QString m_tailscaleName;
    QString m_boundTo;
};

} // namespace tumbler
