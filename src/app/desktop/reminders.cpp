#include "reminders.h"

#include "appcontroller.h"
#include "remote/remotehub.h"
#include "reminderplan.h"
#include "today.h"

#include <KLocalizedString>
#include <KIdleTime>
#include <KNotification>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QLocale>

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;
} // namespace

Reminders::Reminders(AppController *app, RemoteHub *hub, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_hub(hub)
    , m_startedAt(QDateTime::currentMSecsSinceEpoch())
{
    m_timer.setInterval(20 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &Reminders::check);
    connect(app, &AppController::todayChanged, this, &Reminders::check);
    m_timer.start();
}

void Reminders::check()
{
    const QVariantMap settings = m_app->settings();
    if (!settings.value(QStringLiteral("notifications"), true).toBool())
        return;
    const qint64 lead = qint64(settings.value(QStringLiteral("reminderMinutes"), 5).toInt()) * kMinute;
    const qint64 now = m_app->now();
    const TodayView view = buildToday(m_app->queries(), now, m_app->zone(), 0);

    const auto timeText = [this](qint64 ms) {
        return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms, m_app->zone()).time(), QLocale::ShortFormat);
    };

    const QList<Reminder> due = dueReminders(view, now, lead, m_startedAt, m_fired);
    if (due.isEmpty())
        return;
    const QString route = RemoteHub::route(settings.value(QStringLiteral("remoteNotify"), QStringLiteral("away")).toString(),
                                           m_hub && m_hub->hasPhones(), userAway());
    const bool desktop = route != QLatin1String("phone");
    const bool phone = route != QLatin1String("desktop");

    for (const Reminder &r : due) {
        const AgendaItem &item = r.item;
        QString event, text;
        switch (r.kind) {
        case Reminder::Kind::StartingSoon:
            event = QStringLiteral("startingSoon");
            text = i18n("Starts at %1, in %2 min", timeText(item.start), (item.start - now + kMinute - 1) / kMinute);
            break;
        case Reminder::Kind::StartingNow:
            event = QStringLiteral("startingNow");
            text = i18n("Starting now, until %1", timeText(item.end));
            break;
        case Reminder::Kind::WrapUp:
            event = QStringLiteral("wrapUp");
            text = i18n("5 minutes left in this block (until %1)", timeText(item.end));
            break;
        case Reminder::Kind::Missed:
            event = QStringLiteral("overdue");
            text = i18n("This block ended without being marked done");
            break;
        }
        const bool missed = r.kind == Reminder::Kind::Missed;
        if (desktop)
            notify(event, item.title, text, item, missed);
        if (phone)
            pushToPhone(event, item.title, text, item, missed);
    }
}

bool Reminders::userAway() const
{
    // Away: no input for a few minutes, or the screen is locked.
    if (KIdleTime::instance()->idleTime() >= 3 * kMinute)
        return true;
    QDBusInterface saver(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                         QStringLiteral("org.freedesktop.ScreenSaver"), QDBusConnection::sessionBus());
    const QDBusReply<bool> locked = saver.call(QStringLiteral("GetActive"));
    return locked.isValid() && locked.value();
}

void Reminders::pushToPhone(const QString &event, const QString &title, const QString &text, const AgendaItem &item, bool missed)
{
    // Phones show two buttons; these are the two that matter most away from
    // the desk. Each maps to a command the service worker sends back.
    QJsonArray actions;
    QJsonObject commands;
    const auto add = [&](const QString &action, const QString &label, const QJsonObject &command) {
        actions.append(QJsonObject{{QStringLiteral("action"), action}, {QStringLiteral("title"), label}});
        commands.insert(action, command);
    };
    switch (item.kind) {
    case AgendaItem::Kind::Schedule:
        if (missed) {
            add(QStringLiteral("later"), i18n("Later"), {{QStringLiteral("type"), QStringLiteral("bumpNext")}, {QStringLiteral("id"), item.id}});
        }
        add(QStringLiteral("done"), i18n("Done"), {{QStringLiteral("type"), QStringLiteral("completeSchedule")}, {QStringLiteral("id"), item.id}});
        if (!missed) {
            add(QStringLiteral("bump"), i18n("+15 min"),
                {{QStringLiteral("type"), QStringLiteral("bump")}, {QStringLiteral("id"), item.id}, {QStringLiteral("minutes"), 15}});
        }
        break;
    case AgendaItem::Kind::Occurrence:
        add(QStringLiteral("done"), i18n("Done"),
            {{QStringLiteral("type"), QStringLiteral("occurrence")}, {QStringLiteral("id"), item.id}, {QStringLiteral("status"), QStringLiteral("completed")}});
        add(QStringLiteral("skip"), i18n("Skip"),
            {{QStringLiteral("type"), QStringLiteral("occurrence")}, {QStringLiteral("id"), item.id}, {QStringLiteral("status"), QStringLiteral("skipped")}});
        break;
    case AgendaItem::Kind::Event:
        break;
    }
    m_hub->push({{QStringLiteral("title"), title},
                 {QStringLiteral("body"), text},
                 // One notification per item: a later reminder replaces the earlier one.
                 {QStringLiteral("tag"), QStringLiteral("tt-") + item.id},
                 {QStringLiteral("actions"), actions},
                 {QStringLiteral("commands"), commands},
                 {QStringLiteral("event"), event}},
                event == QLatin1String("startingSoon") ? QStringLiteral("normal") : QStringLiteral("high"));
}

void Reminders::notify(const QString &event, const QString &title, const QString &text, const AgendaItem &item, bool missed)
{
    auto *n = new KNotification(event, KNotification::CloseOnTimeout);
    n->setComponentName(QStringLiteral("tasktumbler"));
    n->setTitle(title);
    n->setText(text);
    n->setIconName(QStringLiteral("org.kde.tasktumbler"));
    // Focus mode turns on Do Not Disturb; the end-of-block warning still needs
    // to get through, since losing track of time is the point of it.
    if (event == QLatin1String("wrapUp") && m_app->focusMode())
        n->setUrgency(KNotification::CriticalUrgency);

    QPointer<AppController> app = m_app;
    const QString id = item.id;
    const QString taskId = item.taskId;

    auto *open = n->addDefaultAction(i18n("Open"));
    connect(open, &KNotificationAction::activated, this, [app] {
        if (app)
            Q_EMIT app->showWindowRequested();
    });

    switch (item.kind) {
    case AgendaItem::Kind::Schedule:
        if (missed) {
            connect(n->addAction(i18n("Next free slot")), &KNotificationAction::activated, this, [app, id] {
                if (app)
                    app->bumpToNextSlot(id);
            });
        } else {
            if (event != QLatin1String("wrapUp")) {
                connect(n->addAction(i18n("Start")), &KNotificationAction::activated, this, [app, taskId] {
                    if (app)
                        app->startWork(taskId);
                });
            }
            connect(n->addAction(event == QLatin1String("wrapUp") ? i18n("+15 min") : i18n("Bump 15 min")),
                    &KNotificationAction::activated, this, [app, id] {
                        if (app)
                            app->bump(id, 15);
                    });
        }
        connect(n->addAction(i18n("Done")), &KNotificationAction::activated, this, [app, id] {
            if (app)
                app->completeSchedule(id);
        });
        break;
    case AgendaItem::Kind::Occurrence:
        connect(n->addAction(i18n("Done")), &KNotificationAction::activated, this, [app, id] {
            if (app)
                app->setOccurrence(id, QStringLiteral("completed"));
        });
        connect(n->addAction(i18n("Skip")), &KNotificationAction::activated, this, [app, id] {
            if (app)
                app->setOccurrence(id, QStringLiteral("skipped"));
        });
        break;
    case AgendaItem::Kind::Event:
        break;
    }
    n->sendEvent();
}

} // namespace tumbler
