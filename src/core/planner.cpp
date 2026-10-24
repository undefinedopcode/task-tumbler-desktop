#include "planner.h"

#include <QDateTime>
#include <algorithm>

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;
constexpr qint64 kDay = 24 * 60 * kMinute;

qint64 snapUp(qint64 t, qint64 snap)
{
    if (snap <= 1)
        return t;
    const qint64 r = t % snap;
    return r == 0 ? t : t + (snap - r);
}

qint64 at(QDate day, QTime time, const QTimeZone &zone)
{
    return QDateTime(day, time, zone).toMSecsSinceEpoch();
}
} // namespace

QList<Interval> mergeIntervals(QList<Interval> intervals)
{
    intervals.erase(std::remove_if(intervals.begin(), intervals.end(), [](const Interval &i) { return i.end <= i.start; }),
                    intervals.end());
    std::sort(intervals.begin(), intervals.end(), [](const Interval &a, const Interval &b) { return a.start < b.start; });
    QList<Interval> out;
    for (const Interval &i : intervals) {
        if (!out.isEmpty() && i.start <= out.last().end)
            out.last().end = std::max(out.last().end, i.end);
        else
            out.append(i);
    }
    return out;
}

std::optional<qint64> findSlot(const QList<Interval> &windows, const QList<Interval> &busy, qint64 earliest, qint64 durationMs,
                               qint64 snapMs)
{
    if (durationMs <= 0)
        return std::nullopt;
    const QList<Interval> blocked = mergeIntervals(busy);

    for (const Interval &w : mergeIntervals(windows)) {
        qint64 candidate = snapUp(std::max(w.start, earliest), snapMs);
        while (candidate + durationMs <= w.end) {
            // First busy interval that overlaps the candidate, if any.
            auto clash = std::find_if(blocked.begin(), blocked.end(), [&](const Interval &b) {
                return b.start < candidate + durationMs && b.end > candidate;
            });
            if (clash == blocked.end())
                return candidate;
            candidate = snapUp(clash->end, snapMs);
        }
    }
    return std::nullopt;
}

QList<Interval> categoryWindows(const CategorySchedule &schedule, QDate firstDay, int days, const QTimeZone &zone)
{
    QList<Interval> out;
    for (int i = 0; i < days; ++i) {
        const QDate day = firstDay.addDays(i);
        if (const auto w = schedule.windowFor(day))
            out.append({at(day, w->start, zone), at(day, w->end, zone)});
    }
    return out;
}

std::optional<Interval> nextFreeSlot(const Queries &q, const Task &task, qint64 earliest, const QTimeZone &zone, int searchDays,
                                     const QString &ignoreScheduleId)
{
    const QDate firstDay = QDateTime::fromMSecsSinceEpoch(earliest, zone).date();
    const QDate lastDay = firstDay.addDays(searchDays - 1);
    const qint64 rangeStart = at(firstDay, QTime(0, 0), zone);
    const qint64 rangeEnd = rangeStart + qint64(searchDays) * kDay + kDay;

    // A task without a usable category gets the same hours as a new
    // database's Default category.
    std::optional<Category> category;
    for (const Category &c : q.categories()) {
        if (c.id == task.categoryId || (!category && c.isDefault))
            category = c;
        if (c.id == task.categoryId)
            break;
    }
    const CategorySchedule hours = category ? category->schedule : CategorySchedule::anytime();
    const QList<Interval> windows = categoryWindows(hours, firstDay, searchDays, zone);

    QList<Interval> busy;
    for (const ScheduleBlock &s : q.schedules(rangeStart, rangeEnd)) {
        if (s.id != ignoreScheduleId && !s.completed)
            busy.append({s.start, s.end});
    }

    // Timed recurring items occupy their slot.
    QHash<QString, Task> recurring;
    for (const Task &t : q.tasks()) {
        if (t.isRecurring())
            recurring.insert(t.id, t);
    }
    for (const Occurrence &o : q.occurrences(firstDay, lastDay)) {
        const auto it = recurring.constFind(o.taskId);
        if (it == recurring.constEnd() || o.status != OccurrenceStatus::Pending)
            continue;
        const QTime time = o.scheduledTime.isValid() ? o.scheduledTime : it->scheduledTime;
        if (!time.isValid())
            continue;
        const qint64 s = at(o.date, time, zone);
        busy.append({s, s + qint64(std::max(5, it->estimateMinutes)) * kMinute});
    }

    for (const Event &e : q.events(rangeStart, rangeEnd, firstDay, lastDay)) {
        if (e.blocking.allows(task.isRecurring(), task.tags))
            continue;
        if (e.allDay)
            busy.append({at(e.startDate, QTime(0, 0), zone), at(e.endDate.addDays(1), QTime(0, 0), zone)});
        else
            busy.append({e.startAt, e.endAt});
    }

    const qint64 duration = qint64(std::max(5, task.estimateMinutes)) * kMinute;
    const auto start = findSlot(windows, busy, earliest, duration);
    if (!start)
        return std::nullopt;
    return Interval{*start, *start + duration};
}

} // namespace tumbler
