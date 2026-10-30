// AppController: changes a calendar sync brought (a meeting added, moved or
// cancelled at short notice), kept for Today and announced when they're near.

#include "appcontroller.h"
#include "operations.h"

#include <QDateTime>
#include <QJsonArray>
#include <QLocale>

namespace tumbler {

namespace {

const QString kSetting = QStringLiteral("calendar_changes");

// "14:30", "tomorrow 09:00".
QString when(qint64 ms, qint64 now, const QTimeZone &zone)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms, zone);
    const QString time = QLocale().toString(dt.time(), QLocale::ShortFormat);
    const QDate today = QDateTime::fromMSecsSinceEpoch(now, zone).date();
    if (dt.date() == today)
        return time;
    if (dt.date() == today.addDays(1))
        return QStringLiteral("tomorrow %1").arg(time);
    return QStringLiteral("%1 %2").arg(QLocale().toString(dt.date(), QStringLiteral("ddd d MMM")), time);
}

} // namespace

void AppController::noteCalendarChanges(const QList<CalendarChange> &fresh)
{
    const qint64 t = now();
    const QDate today = QDateTime::fromMSecsSinceEpoch(t, zone()).date();
    // Today and tomorrow: further out is planning, not news.
    const qint64 horizon = QDateTime(today.addDays(2), QTime(0, 0), zone()).toMSecsSinceEpoch();
    QList<CalendarChange> near;
    for (const CalendarChange &c : fresh) {
        if (c.affects() < horizon)
            near.append(c);
    }
    const QJsonArray before = queries().setting(kSetting, QJsonArray()).toArray();
    const QList<CalendarChange> kept = mergeChanges(changesFromJson(before), near, t);
    const QJsonArray after = toJson(kept);
    if (after != before)
        applyExternal(ops::setSetting(kSetting, after));

    for (const CalendarChange &c : near) {
        const auto it = std::find_if(kept.cbegin(), kept.cend(), [&c](const CalendarChange &k) { return k.key == c.key; });
        if (it == kept.cend() || urgencyOf(*it, t) == ChangeUrgency::Later)
            continue; // merged away (moved back), or not near enough to interrupt
        QVariantMap v;
        for (const QVariant &shown : calendarChanges()) {
            if (shown.toMap().value(QStringLiteral("key")).toString() == c.key)
                v = shown.toMap();
        }
        if (!v.isEmpty())
            Q_EMIT calendarAlert(v);
    }
}

QVariantList AppController::calendarChanges() const
{
    const Queries q = queries();
    const qint64 t = now();
    QVariantList out;
    for (const CalendarChange &c : changesFromJson(q.setting(kSetting, QJsonArray()).toArray())) {
        if (c.over() <= t)
            continue;
        QVariantMap m{
            {QStringLiteral("key"), c.key},
            {QStringLiteral("kind"), nameOf(c.kind)},
            {QStringLiteral("title"), c.title},
            {QStringLiteral("eventId"), c.eventId},
            {QStringLiteral("location"), c.location},
            {QStringLiteral("oldLocation"), c.oldLocation},
            {QStringLiteral("start"), c.start},
            {QStringLiteral("end"), c.end},
            {QStringLiteral("when"), when(c.start, t, zone())},
            {QStringLiteral("seenAt"), c.seenAt},
            {QStringLiteral("urgency"), nameOf(urgencyOf(c, t))},
            {QStringLiteral("underway"), c.kind != CalendarChange::Kind::Cancelled && c.start <= t},
        };
        if (c.oldStart) {
            m.insert(QStringLiteral("oldStart"), c.oldStart);
            m.insert(QStringLiteral("oldWhen"), when(c.oldStart, t, zone()));
        }
        // Planned work it now runs into.
        QVariantList clashes;
        if (c.kind != CalendarChange::Kind::Cancelled) {
            for (const ScheduleBlock &s : q.schedules(c.start, c.end)) {
                if (s.completed)
                    continue;
                const auto task = q.task(s.taskId);
                clashes.append(QVariantMap{{QStringLiteral("blockId"), s.id},
                                           {QStringLiteral("taskId"), s.taskId},
                                           {QStringLiteral("title"), task ? task->title : QString()},
                                           {QStringLiteral("when"), when(s.start, t, zone())}});
            }
        }
        m.insert(QStringLiteral("clashes"), clashes);
        out.append(m);
    }
    return out;
}

void AppController::dismissCalendarChange(const QString &key)
{
    QList<CalendarChange> kept = changesFromJson(queries().setting(kSetting, QJsonArray()).toArray());
    kept.erase(std::remove_if(kept.begin(), kept.end(), [&key](const CalendarChange &c) { return c.key == key; }), kept.end());
    applyExternal(ops::setSetting(kSetting, toJson(kept)));
}

void AppController::dismissCalendarChanges()
{
    applyExternal(ops::setSetting(kSetting, QJsonArray()));
}

bool AppController::moveAfterEvent(const QString &blockId, const QString &key)
{
    for (const CalendarChange &c : changesFromJson(queries().setting(kSetting, QJsonArray()).toArray())) {
        if (c.key == key && c.kind != CalendarChange::Kind::Cancelled)
            return reschedule(blockId, c.end, 0);
    }
    return false;
}

std::optional<qint64> AppController::workStart(QDate day) const
{
    // The categories shown on the schedule are the working ones; the default
    // (often all day) only counts when there are none.
    std::optional<QTime> earliest;
    const QList<Category> all = queries().categories();
    const bool anyShown = std::any_of(all.cbegin(), all.cend(), [](const Category &c) { return c.showOnSchedule; });
    for (const Category &c : all) {
        if (anyShown ? !c.showOnSchedule : !c.isDefault)
            continue;
        if (const auto w = c.schedule.windowFor(day); w && (!earliest || w->start < *earliest))
            earliest = w->start;
    }
    if (!earliest)
        return std::nullopt;
    return QDateTime(day, *earliest, zone()).toMSecsSinceEpoch();
}

} // namespace tumbler
