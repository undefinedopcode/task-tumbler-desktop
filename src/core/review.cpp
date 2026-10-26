#include "review.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>

namespace tumbler::review {

namespace {

constexpr qint64 kDayMs = 24 * 60 * 60 * 1000LL;

qint64 startOf(QDate d, const QTimeZone &zone)
{
    return QDateTime(d, QTime(0, 0), zone).toMSecsSinceEpoch();
}

QList<Win> winsOf(const stats::Summary &s, const QHash<QString, int> &logged)
{
    QList<Win> out;
    for (const stats::Finished &f : s.finished)
        out.append(Win{f.taskId, f.title, f.at, f.routine, logged.value(f.taskId)});
    return out;
}

// Worked on in the range and still open; routines finished in the range are
// wins already.
QList<Progress> progressOf(const stats::Summary &s, const QHash<QString, Task> &tasks, const QList<Win> &wins)
{
    QSet<QString> won;
    for (const Win &w : wins)
        won.insert(w.taskId);
    QList<Progress> out;
    for (const stats::TaskTime &t : s.tasks) { // most worked first already
        const auto task = tasks.constFind(t.taskId);
        if (t.workedMinutes <= 0 || won.contains(t.taskId) || task == tasks.cend() || isFinished(task->status))
            continue;
        out.append(Progress{t.taskId, t.title, t.workedMinutes});
    }
    return out;
}

QHash<QString, Task> tasksById(const Queries &q)
{
    QHash<QString, Task> out;
    for (const Task &t : q.tasks())
        out.insert(t.id, t);
    return out;
}

} // namespace

DayReview reviewDay(const Queries &q, QDate day, const QTimeZone &zone, int lookbackDays)
{
    DayReview r;
    r.date = day;
    const qint64 dayStart = startOf(day, zone);
    const qint64 dayEnd = startOf(day.addDays(1), zone);
    const QHash<QString, Task> tasks = tasksById(q);

    const stats::Summary s = stats::summarize(q, day, day, zone);
    r.workedMinutes = s.workedMinutes;
    r.wins = winsOf(s, q.loggedMinutes());
    r.progress = progressOf(s, tasks, r.wins);

    // Blocks that didn't get done, while their task is still open.
    for (const ScheduleBlock &b : q.schedules(startOf(day.addDays(-lookbackDays), zone), dayEnd)) {
        const auto task = tasks.constFind(b.taskId);
        if (b.completed || b.start >= dayEnd || task == tasks.cend() || isFinished(task->status))
            continue;
        Leftover l;
        l.kind = Leftover::Kind::Block;
        l.id = b.id;
        l.taskId = b.taskId;
        l.title = task->title;
        l.start = b.start;
        l.end = b.end;
        l.earlier = b.start < dayStart;
        l.bumps = b.bumpedCount;
        r.leftovers.append(l);
    }
    for (const Occurrence &o : q.occurrences(day, day)) {
        const auto task = tasks.constFind(o.taskId);
        if (o.status != OccurrenceStatus::Pending || task == tasks.cend())
            continue;
        const QTime time = o.scheduledTime.isValid() ? o.scheduledTime : task->scheduledTime;
        Leftover l;
        l.kind = Leftover::Kind::Routine;
        l.id = o.id;
        l.taskId = o.taskId;
        l.title = task->title;
        l.timeless = !time.isValid();
        l.start = QDateTime(day, time.isValid() ? time : QTime(9, 0), zone).toMSecsSinceEpoch();
        l.end = l.start + qint64(std::max(5, task->estimateMinutes)) * 60000;
        r.leftovers.append(l);
    }
    // By time, with routines that have no time of day at the end.
    std::sort(r.leftovers.begin(), r.leftovers.end(), [](const Leftover &a, const Leftover &b) {
        if (a.timeless != b.timeless)
            return b.timeless;
        return a.start != b.start ? a.start < b.start : a.title < b.title;
    });

    // A glance at tomorrow, so it isn't a surprise.
    Tomorrow &t = r.tomorrow;
    t.date = day.addDays(1);
    const qint64 tStart = dayEnd;
    const qint64 tEnd = startOf(t.date.addDays(1), zone);
    const auto consider = [&t](const QString &title, qint64 at) {
        if (!t.firstAt || at < t.firstAt) {
            t.firstAt = at;
            t.firstTitle = title;
        }
    };
    for (const ScheduleBlock &b : q.schedules(tStart, tEnd)) {
        if (b.completed || b.start < tStart)
            continue;
        ++t.blocks;
        consider(tasks.value(b.taskId).title, b.start);
    }
    for (const Occurrence &o : q.occurrences(t.date, t.date)) {
        const auto task = tasks.constFind(o.taskId);
        if (o.status != OccurrenceStatus::Pending || task == tasks.cend())
            continue;
        ++t.routines;
        const QTime time = o.scheduledTime.isValid() ? o.scheduledTime : task->scheduledTime;
        if (time.isValid())
            consider(task->title, QDateTime(t.date, time, zone).toMSecsSinceEpoch());
    }
    for (const Event &e : q.events(tStart, tEnd, t.date, t.date)) {
        ++t.events;
        if (!e.allDay && e.startAt >= tStart)
            consider(e.title, e.startAt);
    }
    return r;
}

WeekReview reviewWeek(const Queries &q, QDate from, QDate to, const QTimeZone &zone)
{
    WeekReview w;
    w.summary = stats::summarize(q, from, to, zone);
    const QHash<QString, Task> tasks = tasksById(q);
    w.wins = winsOf(w.summary, q.loggedMinutes());
    w.progress = progressOf(w.summary, tasks, w.wins);

    // "Coming up" is anything not done that ends after the last day starts.
    const qint64 lastDay = startOf(to, zone);
    QSet<QString> planned;
    for (const ScheduleBlock &b : q.schedules(lastDay, std::numeric_limits<qint64>::max())) {
        if (!b.completed)
            planned.insert(b.taskId);
    }
    QSet<QString> openParents;
    for (const Task &t : tasks) {
        if (!t.parentId.isEmpty() && !isFinished(t.status))
            openParents.insert(t.parentId);
    }

    QSqlQuery touched(q.db());
    touched.prepare(QStringLiteral("SELECT t.id, MAX(t.updated_at,"
                                   " COALESCE((SELECT MAX(at) FROM task_history h WHERE h.task_id = t.id), 0),"
                                   " COALESCE((SELECT MAX(started_at) FROM work_logs l WHERE l.task_id = t.id), 0)) "
                                   "FROM tasks t WHERE t.status IN ('pending', 'in_progress') AND t.recurrence IS NULL"));
    touched.exec();
    const qint64 end = startOf(to.addDays(1), zone);
    while (touched.next()) {
        const QString id = touched.value(0).toString();
        const int days = int((end - touched.value(1).toLongLong()) / kDayMs);
        // A parent waiting on its steps is being dealt with through them.
        if (days >= kStaleDays && !planned.contains(id) && !openParents.contains(id))
            w.stale.append(Stale{id, tasks.value(id).title, days});
    }
    std::sort(w.stale.begin(), w.stale.end(), [](const Stale &a, const Stale &b) {
        return a.days != b.days ? a.days > b.days : a.title < b.title;
    });
    w.stale = w.stale.mid(0, 5);

    for (const Task &t : tasks) {
        if (isFinished(t.status) || t.isRecurring() || !t.dueDate.isValid() || planned.contains(t.id) || openParents.contains(t.id))
            continue;
        if (t.dueDate <= to.addDays(7))
            w.dueSoon.append(DueSoon{t.id, t.title, t.dueDate});
    }
    std::sort(w.dueSoon.begin(), w.dueSoon.end(), [](const DueSoon &a, const DueSoon &b) {
        return a.due != b.due ? a.due < b.due : a.title < b.title;
    });
    return w;
}

} // namespace tumbler::review
