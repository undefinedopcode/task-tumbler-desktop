#include "reminders.h"

#include "appcontroller.h"
#include "reminderplan.h"
#include "today.h"

#include <KLocalizedString>
#include <KNotification>
#include <QDateTime>
#include <QLocale>

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;
} // namespace

Reminders::Reminders(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
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

    for (const Reminder &r : dueReminders(view, now, lead, m_startedAt, m_fired)) {
        const AgendaItem &item = r.item;
        switch (r.kind) {
        case Reminder::Kind::StartingSoon:
            notify(QStringLiteral("startingSoon"), item.title,
                   i18n("Starts at %1, in %2 min", timeText(item.start), (item.start - now + kMinute - 1) / kMinute), item);
            break;
        case Reminder::Kind::StartingNow:
            notify(QStringLiteral("startingNow"), item.title, i18n("Starting now, until %1", timeText(item.end)), item);
            break;
        case Reminder::Kind::WrapUp:
            notify(QStringLiteral("wrapUp"), item.title, i18n("5 minutes left in this block (until %1)", timeText(item.end)), item);
            break;
        case Reminder::Kind::Missed:
            notify(QStringLiteral("overdue"), item.title, i18n("This block ended without being marked done"), item, true);
            break;
        }
    }
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
