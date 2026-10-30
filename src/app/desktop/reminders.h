#pragma once

#include <QDate>
#include <QVariantMap>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace tumbler {

class AppController;
class RemoteHub;
struct AgendaItem;

// Desktop notifications for today's agenda, with buttons that act directly:
// Start / Bump 15m / Done for blocks, Done / Skip for recurring items, Next
// free slot / Done for a missed block. When each fires is decided by
// dueReminders() in core.
class Reminders : public QObject
{
    Q_OBJECT
public:
    // With a hub, reminders can go to paired phones instead of (or as well
    // as) the desktop, depending on the remote_notify setting.
    explicit Reminders(AppController *app, RemoteHub *hub = nullptr, QObject *parent = nullptr);

private:
    void check();
    // Once a day, when the close-out time comes and the day isn't closed yet.
    void nudgeCloseOut(bool desktop, bool phone);
    void notify(const QString &event, const QString &title, const QString &text, const AgendaItem &item, bool missed = false);
    void pushToPhone(const QString &event, const QString &title, const QString &text, const AgendaItem &item, bool missed);
    // A calendar change near enough to interrupt for (AppController::calendarAlert).
    void calendarAlert(const QVariantMap &change);
    // At the start of the working day: changes that came in overnight and
    // affect its first hour, which no alert has covered yet.
    void startOfDayBrief(QDate day, qint64 now);
    void notifyCalendar(const QString &title, const QString &text, bool urgent, const QVariantMap &change);
    // Where notifications go now: "desktop", "phone" or "both".
    QString route() const;
    bool userAway() const;

    AppController *m_app;
    RemoteHub *m_hub;
    QTimer m_timer;
    QSet<QString> m_fired;
    qint64 m_startedAt;
    QDate m_closeOutNudged;
    QDate m_briefed;
};

} // namespace tumbler
