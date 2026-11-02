#pragma once

#include "icsfeed.h"
#include "linear.h"

#include <QNetworkAccessManager>
#include <QObject>
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

    // {syncing, linear: {enabled, user, teams, teamKeys, includeBacklog, error, lastSync, summary},
    //  feeds: [{id, name, url, color, blocking, enabled, dayZone, error, lastSync, count, declaredZone}]}
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

public:
    SyncHub(AppController *app, KeyStore *keys, QObject *parent = nullptr);

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

    // Calendars already on this computer (macOS: Calendar.app's, through
    // EventKit). {supported, access: "notAsked"|"granted"|"denied",
    //  calendars: [{id, title, account, color, feedId}]}; feedId is set for
    // the ones already added.
    Q_INVOKABLE QVariantMap deviceCalendars() const;
    // The system's one-time prompt; answers with deviceCalendarsChanged().
    Q_INVOKABLE void requestCalendarAccess();
    Q_INVOKABLE void addDeviceCalendar(const QString &calendarId);

Q_SIGNALS:
    void stateChanged();
    void syncFinished();
    void feedAdded(const QString &id);
    void feedFailed(const QString &error);
    void deviceCalendarsChanged();

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
    // A device: feed reads the computer's calendar; others are fetched.
    void refreshFeed(const sync::FeedSettings &feed, std::function<void()> done);
    void applyFeed(const sync::FeedSettings &feed, const QList<sync::RemoteEvent> &events, qint64 from, qint64 to, const QString &parseError);
    void syncFeeds(std::function<void()> done);
    void syncDeviceFeeds();
    void watchDeviceCalendars();
    void finishOne();

    AppController *m_app;
    KeyStore *m_keys;
    QNetworkAccessManager m_network;
    sync::LinearClient m_linear;
    sync::FeedFetcher m_fetcher;
    QTimer m_every;
    QTimer m_feedsEvery; // calendars more often: short-notice changes matter
    QTimer m_push;
    QTimer m_deviceChanged; // calendar edits come in bursts
    bool m_watchingDevice = false;
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
        QString declaredZone; // what the calendar file says, for the settings
    };
    QHash<QString, FeedStatus> m_feedStatus;
};

} // namespace tumbler
