#include "reminders.h"

#include "appcontroller.h"
#include "presence.h"
#include "remote/remotehub.h"
#include "reminderplan.h"
#include "today.h"

#include <KLocalizedString>
#include <KNotification>
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
    connect(app, &AppController::calendarAlert, this, &Reminders::calendarAlert);
    m_timer.start();
}

QString Reminders::route() const
{
    const QVariantMap settings = m_app->settings();
    return RemoteHub::route(settings.value(QStringLiteral("remoteNotify"), QStringLiteral("away")).toString(), m_hub && m_hub->hasPhones(),
                            userAway());
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

    startOfDayBrief(view.date, now);

    const QList<Reminder> due = dueReminders(view, now, lead, m_startedAt, m_fired);
    const bool closeOut = m_app->today().value(QStringLiteral("closeOutDue")).toBool() && m_closeOutNudged != view.date;
    if (due.isEmpty() && !closeOut)
        return;
    const QString where = route();
    const bool desktop = where != QLatin1String("phone");
    const bool phone = where != QLatin1String("desktop");
    if (closeOut) {
        m_closeOutNudged = view.date;
        nudgeCloseOut(desktop, phone);
    }

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

void Reminders::nudgeCloseOut(bool desktop, bool phone)
{
    const QString title = i18n("Winding down?");
    const QString text = i18n("Take a couple of minutes to close out the day: what got done, and where the rest goes.");
    if (phone) {
        // "Close out" has no command, so tapping it opens the close-out view.
        m_hub->push({{QStringLiteral("title"), title},
                     {QStringLiteral("body"), text},
                     {QStringLiteral("tag"), QStringLiteral("tt-closeout")},
                     {QStringLiteral("url"), QStringLiteral("/#closeout")},
                     {QStringLiteral("actions"), QJsonArray{QJsonObject{{QStringLiteral("action"), QStringLiteral("open")}, {QStringLiteral("title"), i18n("Close out")}},
                                                            QJsonObject{{QStringLiteral("action"), QStringLiteral("later")}, {QStringLiteral("title"), i18n("Not today")}}}},
                     {QStringLiteral("commands"), QJsonObject{{QStringLiteral("later"), QJsonObject{{QStringLiteral("type"), QStringLiteral("finishCloseOut")}, {QStringLiteral("quietly"), true}}}}},
                     {QStringLiteral("event"), QStringLiteral("closeOut")}},
                    QStringLiteral("normal"));
    }
    if (!desktop)
        return;
    auto *n = new KNotification(QStringLiteral("closeOut"), KNotification::CloseOnTimeout);
    n->setComponentName(QStringLiteral("tasktumbler"));
    n->setTitle(title);
    n->setText(text);
    n->setIconName(QStringLiteral("org.kde.tasktumbler"));
    QPointer<AppController> app = m_app;
    const auto open = [app] {
        if (!app)
            return;
        Q_EMIT app->showWindowRequested();
        Q_EMIT app->pageRequested(QStringLiteral("closeout"));
    };
    connect(n->addDefaultAction(i18n("Close out")), &KNotificationAction::activated, this, open);
    connect(n->addAction(i18n("Close out")), &KNotificationAction::activated, this, open);
    connect(n->addAction(i18n("Not today")), &KNotificationAction::activated, this, [app] {
        if (app)
            app->finishCloseOut(true);
    });
    n->sendEvent();
}

bool Reminders::userAway() const
{
    // Away: no input for a few minutes, or the screen is locked.
    return presence::away(3);
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

// ---- calendar changes ------------------------------------------------------------

namespace {

// Title and one line for a change, as calendarChanges() describes it.
QPair<QString, QString> describe(const QVariantMap &c)
{
    const QString kind = c.value(QStringLiteral("kind")).toString();
    const QString title = c.value(QStringLiteral("title")).toString();
    const QString when = c.value(QStringLiteral("when")).toString();
    const QString where = c.value(QStringLiteral("location")).toString();
    QString head, text;
    if (kind == QLatin1String("added")) {
        head = i18n("New: %1", title);
        text = where.isEmpty() ? i18n("At %1", when) : i18n("At %1, %2", when, where);
    } else if (kind == QLatin1String("moved")) {
        head = i18n("Moved: %1", title);
        text = i18n("Now %1 (was %2)", when, c.value(QStringLiteral("oldWhen")).toString());
    } else if (kind == QLatin1String("cancelled")) {
        head = i18n("Cancelled: %1", title);
        text = i18n("It was at %1; that time is free now", when);
    } else {
        head = i18n("New place: %1", title);
        text = i18n("At %1, now in %2", when, where.isEmpty() ? i18n("no set place") : where);
    }
    const QVariantList clashes = c.value(QStringLiteral("clashes")).toList();
    if (clashes.size() == 1)
        text += QLatin1String(". ") + i18n("Clashes with “%1”", clashes.first().toMap().value(QStringLiteral("title")).toString());
    else if (clashes.size() > 1)
        text += QLatin1String(". ") + i18n("Clashes with %1 planned blocks", clashes.size());
    return {head, text};
}

} // namespace

void Reminders::calendarAlert(const QVariantMap &change)
{
    if (!m_app->settings().value(QStringLiteral("notifications"), true).toBool())
        return;
    const auto [title, text] = describe(change);
    // Very soon, and something to go to: keep at it until seen. A cancelled
    // meeting frees time; worth knowing, not worth nagging about.
    const bool urgent = change.value(QStringLiteral("urgency")).toString() == QLatin1String("soon")
        && change.value(QStringLiteral("kind")).toString() != QLatin1String("cancelled");
    notifyCalendar(title, text, urgent, change);
}

void Reminders::startOfDayBrief(QDate day, qint64 now)
{
    if (m_briefed == day)
        return;
    const auto start = m_app->workStart(day);
    if (!start || now < *start)
        return;
    m_briefed = day;
    if (now >= *start + 60 * kMinute)
        return; // opened later in the day: Today's box has it

    QStringList lines;
    QVariantMap first;
    for (const QVariant &v : m_app->calendarChanges()) {
        const QVariantMap c = v.toMap();
        const qint64 seenAt = c.value(QStringLiteral("seenAt")).toLongLong();
        qint64 affects = c.value(QStringLiteral("start")).toLongLong();
        if (c.contains(QStringLiteral("oldStart")))
            affects = std::min(affects, c.value(QStringLiteral("oldStart")).toLongLong());
        // Came in before the day started, bites in its first hour, and was
        // too far off back then to have been announced.
        if (seenAt >= *start || affects >= *start + 60 * kMinute || affects - seenAt <= 120 * kMinute)
            continue;
        const auto [head, text] = describe(c);
        lines.append(QStringLiteral("%1. %2").arg(head, text));
        if (first.isEmpty())
            first = c;
    }
    if (lines.isEmpty())
        return;
    if (lines.size() == 1) {
        const auto [head, text] = describe(first);
        notifyCalendar(head, text, false, first);
    } else {
        notifyCalendar(i18n("Calendar changes since yesterday"), lines.join(QLatin1Char('\n')), false, {});
    }
}

void Reminders::notifyCalendar(const QString &title, const QString &text, bool urgent, const QVariantMap &change)
{
    const QString where = route();
    const QString key = change.value(QStringLiteral("key")).toString();
    const QVariantList clashes = change.value(QStringLiteral("clashes")).toList();
    const QString blockId = clashes.size() == 1 ? clashes.first().toMap().value(QStringLiteral("blockId")).toString() : QString();

    if (where != QLatin1String("desktop") && m_hub) {
        QJsonArray actions;
        QJsonObject commands;
        if (!blockId.isEmpty()) {
            actions.append(QJsonObject{{QStringLiteral("action"), QStringLiteral("after")}, {QStringLiteral("title"), i18n("Move my block after it")}});
            commands.insert(QStringLiteral("after"), QJsonObject{{QStringLiteral("type"), QStringLiteral("moveAfterEvent")},
                                                                 {QStringLiteral("id"), blockId},
                                                                 {QStringLiteral("key"), key}});
        }
        m_hub->push({{QStringLiteral("title"), title},
                     {QStringLiteral("body"), text},
                     {QStringLiteral("tag"), QStringLiteral("tt-cal-") + (key.isEmpty() ? QStringLiteral("brief") : key)},
                     {QStringLiteral("actions"), actions},
                     {QStringLiteral("commands"), commands},
                     {QStringLiteral("event"), QStringLiteral("calendarChange")}},
                    QStringLiteral("high"));
    }
    if (where == QLatin1String("phone"))
        return;

    auto *n = new KNotification(QStringLiteral("calendarChange"), urgent ? KNotification::Persistent : KNotification::CloseOnTimeout);
    n->setComponentName(QStringLiteral("tasktumbler"));
    n->setTitle(title);
    n->setText(text);
    n->setIconName(QStringLiteral("org.kde.tasktumbler"));
    // Gets through Do Not Disturb and focus mode: missing the meeting is
    // worse than the interruption.
    n->setUrgency(urgent ? KNotification::CriticalUrgency : KNotification::HighUrgency);
    QPointer<AppController> app = m_app;
    const auto open = [app] {
        if (!app)
            return;
        Q_EMIT app->showWindowRequested();
        Q_EMIT app->pageRequested(QStringLiteral("today"));
    };
    connect(n->addDefaultAction(i18n("Open")), &KNotificationAction::activated, this, open);
    if (!blockId.isEmpty()) {
        connect(n->addAction(i18n("Move my block after it")), &KNotificationAction::activated, this, [app, blockId, key] {
            if (app)
                app->moveAfterEvent(blockId, key);
        });
    }
    if (!key.isEmpty()) {
        connect(n->addAction(i18n("Got it")), &KNotificationAction::activated, this, [app, key] {
            if (app)
                app->dismissCalendarChange(key);
        });
    }
    n->sendEvent();
    Q_EMIT m_app->attentionRequested(urgent);
}

} // namespace tumbler
