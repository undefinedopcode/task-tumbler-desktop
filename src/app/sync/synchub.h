#pragma once

#include "icsfeed.h"
#include "linear.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

namespace tumbler {

class AppController;
class KeyStore;

// Background sync with Linear and subscribed calendars (Phase 4).
// Settings: "linear" {enabled, user, teams, teamKeys, includeBacklog} and
// "calendar_feeds" [FeedSettings]. The Linear API key is in the keyring,
// read only once Linear is connected. Pulls run every 15 minutes, at start
// and on demand; finishing a linked task is pushed back within seconds.
class SyncHub : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // {syncing, linear: {enabled, user, teams, teamKeys, includeBacklog, error, lastSync, summary},
    //  feeds: [{id, name, url, color, blocking, enabled, error, lastSync, count}]}
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

public:
    SyncHub(AppController *app, KeyStore *keys, QObject *parent = nullptr);

    static SyncHub *create(QQmlEngine *, QJSEngine *);
    static void setInstance(SyncHub *hub);

    QVariantMap state() const;
    // Off in screenshot mode and tests that don't want timers.
    void setAutomatic(bool on);
    void setLinearEndpoint(const QUrl &url) { m_linear.setEndpoint(url); }

    Q_INVOKABLE void connectLinear(const QString &apiKey);
    Q_INVOKABLE void disconnectLinear();
    Q_INVOKABLE void setLinearTeams(const QStringList &keys);
    Q_INVOKABLE void setLinearBacklog(bool include);

    // Checks the address works before saving it. Returns the new id.
    Q_INVOKABLE void addFeed(const QVariantMap &feed);
    Q_INVOKABLE void updateFeed(const QString &id, const QVariantMap &changes);
    Q_INVOKABLE void removeFeed(const QString &id);

    Q_INVOKABLE void syncNow();

Q_SIGNALS:
    void stateChanged();
    void syncFinished();
    void feedAdded(const QString &id);
    void feedFailed(const QString &error);

private:
    QJsonObject linearSettings() const;
    void saveLinearSettings(const QJsonObject &s);
    QList<sync::FeedSettings> feeds() const;
    void saveFeeds(const QList<sync::FeedSettings> &feeds);
    sync::LinearFilter linearFilter() const;
    QString integration(const QString &provider);

    void withLinearKey(std::function<void()> then);
    void syncLinear(std::function<void()> done);
    void pushCompletions();
    void pushNext(const QString &integrationId, QList<sync::PendingPush> pending);
    void syncFeed(const sync::FeedSettings &feed, const QByteArray &data, QString *error);
    void syncFeeds(std::function<void()> done);
    void finishOne();

    AppController *m_app;
    KeyStore *m_keys;
    QNetworkAccessManager m_network;
    sync::LinearClient m_linear;
    sync::FeedFetcher m_fetcher;
    QTimer m_every;
    QTimer m_push;
    bool m_keyLoaded = false;
    bool m_pushing = false;
    int m_running = 0;

    QString m_linearError;
    qint64 m_linearLast = 0;
    QString m_linearSummary;
    struct FeedStatus {
        QString error;
        qint64 lastSync = 0;
        int count = 0;
    };
    QHash<QString, FeedStatus> m_feedStatus;
};

} // namespace tumbler
