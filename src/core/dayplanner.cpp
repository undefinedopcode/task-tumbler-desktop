#include "dayplanner.h"

#include "dependencygraph.h"

#include <QDateTime>
#include <QLocale>
#include <algorithm>

namespace tumbler {

namespace {

constexpr qint64 kMinute = 60 * 1000;

qint64 at(QDate day, QTime time, const QTimeZone &zone)
{
    return QDateTime(day, time, zone).toMSecsSinceEpoch();
}

int priorityRank(Priority p)
{
    switch (p) {
    case Priority::Urgent:
        return 0;
    case Priority::High:
        return 1;
    case Priority::Medium:
        return 2;
    case Priority::Low:
        return 3;
    }
    return 2;
}

bool demanding(Energy e)
{
    return e == Energy::High || e == Energy::Peak;
}

// Windows minus the peak window, and windows intersected with it.
QList<Interval> clip(const QList<Interval> &windows, const Interval &peak, bool inside)
{
    QList<Interval> out;
    for (const Interval &w : windows) {
        if (inside) {
            const Interval i{std::max(w.start, peak.start), std::min(w.end, peak.end)};
            if (i.end > i.start)
                out.append(i);
        } else {
            if (peak.start > w.start)
                out.append({w.start, std::min(w.end, peak.start)});
            if (peak.end < w.end)
                out.append({std::max(w.start, peak.end), w.end});
        }
    }
    return out;
}

} // namespace

QList<Task> planCandidates(const Queries &q, QDate day, const QTimeZone &zone)
{
    const qint64 dayStart = at(day, QTime(0, 0), zone);
    QSet<QString> planned;
    for (const ScheduleBlock &s : q.schedules(dayStart, std::numeric_limits<qint64>::max())) {
        if (!s.completed)
            planned.insert(s.taskId);
    }
    const QList<Task> all = q.tasks();
    QHash<QString, int> openSteps;
    for (const Task &t : all) {
        if (!t.parentId.isEmpty() && !isFinished(t.status))
            ++openSteps[t.parentId];
    }
    QList<Task> out;
    for (const Task &t : all) {
        if (!t.isRecurring() && !isFinished(t.status) && !planned.contains(t.id) && openSteps.value(t.id) == 0)
            out.append(t);
    }
    return out;
}

DayPlan planDay(const Queries &q, const PlanOptions &options, const QTimeZone &zone)
{
    DayPlan plan;
    plan.day = options.day;
    const qint64 dayStart = at(options.day, QTime(0, 0), zone);
    const qint64 dayEnd = at(options.day.addDays(1), QTime(0, 0), zone);
    const qint64 earliest = std::max(options.earliest, dayStart);
    const bool lowEnergy = options.energyLevel && *options.energyLevel <= 4;
    const PlanGuidance guidance = options.guidance.value_or(PlanGuidance{});
    plan.explanation = guidance.explanation;

    QHash<QString, Task> tasks;
    for (const Task &t : q.tasks())
        tasks.insert(t.id, t);

    // ---- busy time -------------------------------------------------------------
    QList<Interval> fixedBusy; // blocks and timed recurring items: busy for everyone
    for (const ScheduleBlock &s : q.schedules(dayStart, dayEnd)) {
        if (!s.completed)
            fixedBusy.append({s.start, s.end});
    }
    for (const Occurrence &o : q.occurrences(options.day, options.day)) {
        const auto it = tasks.constFind(o.taskId);
        if (it == tasks.constEnd() || o.status != OccurrenceStatus::Pending)
            continue;
        const QTime time = o.scheduledTime.isValid() ? o.scheduledTime : it->scheduledTime;
        if (!time.isValid())
            continue;
        const qint64 s = at(options.day, time, zone);
        fixedBusy.append({s, s + qint64(std::max(5, it->estimateMinutes)) * kMinute});
    }
    const QList<Event> events = q.events(dayStart, dayEnd, options.day, options.day);

    // ---- candidates ------------------------------------------------------------
    DependencyGraph graph;
    for (const Dependency &d : q.dependencies())
        graph.add(d.taskId, d.dependsOnId);

    QList<Task> candidates;
    for (const Task &t : planCandidates(q, options.day, zone)) {
        if (guidance.defer.contains(t.id)) {
            plan.unplaced.append({t.id, t.title, guidance.defer.value(t.id)});
            continue;
        }
        if (lowEnergy && t.energy == Energy::Peak) {
            plan.unplaced.append({t.id, t.title, QStringLiteral("Needs peak energy; saved for a better day")});
            continue;
        }
        candidates.append(t);
    }

    // ---- ranking ---------------------------------------------------------------
    QHash<QString, int> guided;
    for (int i = 0; i < guidance.order.size(); ++i)
        guided.insert(guidance.order.at(i), i);
    const auto dueRank = [&options](const Task &t) {
        if (!t.dueDate.isValid())
            return 2;
        return t.dueDate <= options.day ? 0 : 1;
    };
    std::sort(candidates.begin(), candidates.end(), [&](const Task &a, const Task &b) {
        const bool ag = guided.contains(a.id), bg = guided.contains(b.id);
        if (ag != bg)
            return ag;
        if (ag)
            return guided.value(a.id) < guided.value(b.id);
        const bool as = a.status == TaskStatus::InProgress, bs = b.status == TaskStatus::InProgress;
        if (as != bs)
            return as;
        if (dueRank(a) != dueRank(b))
            return dueRank(a) < dueRank(b);
        if (a.priority != b.priority)
            return priorityRank(a.priority) < priorityRank(b.priority);
        if (lowEnergy && demanding(a.energy) != demanding(b.energy))
            return !demanding(a.energy);
        if (a.dueDate != b.dueDate)
            return a.dueDate.isValid() && (!b.dueDate.isValid() || a.dueDate < b.dueDate);
        if (a.createdAt != b.createdAt)
            return a.createdAt < b.createdAt;
        return a.id < b.id; // fully deterministic, whatever the hash order
    });

    // ---- placement -------------------------------------------------------------
    QHash<QString, Category> categories;
    std::optional<Category> fallback;
    for (const Category &c : q.categories()) {
        categories.insert(c.id, c);
        if (c.isDefault)
            fallback = c;
    }
    const std::optional<Interval> peak = options.peak
        ? std::optional<Interval>(Interval{at(options.day, options.peak->start, zone), at(options.day, options.peak->end, zone)})
        : std::nullopt;

    QList<Interval> placedBusy;               // planned blocks plus their breaks
    QHash<QString, qint64> placedEnd;         // task id -> end of its planned block
    const qint64 breakMs = qint64(std::max(0, options.breakMinutes)) * kMinute;

    // Several passes so a prerequisite that ranks lower than its dependent
    // still gets placed first; a pass that places nothing ends the loop.
    QList<Task> pending = candidates;
    bool progressed = true;
    while (!pending.isEmpty() && progressed && plan.blocks.size() < options.maxBlocks) {
        progressed = false;
        QList<Task> waiting;
        for (const Task &t : std::as_const(pending)) {
            if (plan.blocks.size() >= options.maxBlocks) {
                waiting.append(t);
                continue;
            }
            // Prerequisites: finished, or planned earlier in this plan.
            qint64 notBefore = earliest;
            bool ready = true;
            for (const QString &p : graph.prerequisitesOf(t.id)) {
                const Task pt = tasks.value(p);
                if (isFinished(pt.status))
                    continue;
                if (!placedEnd.contains(p)) {
                    ready = false;
                    break;
                }
                notBefore = std::max(notBefore, placedEnd.value(p));
            }
            if (!ready) {
                waiting.append(t);
                continue;
            }

            const Category cat = categories.value(t.categoryId, fallback.value_or(Category{}));
            const CategorySchedule hours = categories.contains(t.categoryId) || fallback ? cat.schedule : CategorySchedule::anytime();
            QList<Interval> windows = categoryWindows(hours, options.day, 1, zone);

            QList<Interval> busy = fixedBusy + placedBusy;
            for (const Event &e : events) {
                if (e.blocking.allows(false, t.tags))
                    continue;
                if (e.allDay)
                    busy.append({dayStart, dayEnd});
                else
                    busy.append({e.startAt, e.endAt});
            }

            const qint64 duration = qint64(std::max(5, t.estimateMinutes)) * kMinute;
            std::optional<qint64> start;
            QString where;
            if (peak) {
                if (demanding(t.energy)) {
                    start = findSlot(clip(windows, *peak, true), busy, notBefore, duration);
                    if (start)
                        where = QStringLiteral("in your best hours");
                } else if (t.energy == Energy::Low) {
                    start = findSlot(clip(windows, *peak, false), busy, notBefore, duration);
                }
            }
            if (!start)
                start = findSlot(windows, busy, notBefore, duration);
            if (!start) {
                waiting.append(t);
                continue;
            }

            QStringList why;
            if (guidance.notes.contains(t.id))
                why.append(guidance.notes.value(t.id));
            else {
                if (t.status == TaskStatus::InProgress)
                    why.append(QStringLiteral("Already started"));
                if (t.dueDate.isValid() && t.dueDate < options.day)
                    why.append(QStringLiteral("Overdue"));
                else if (t.dueDate == options.day)
                    why.append(QStringLiteral("Due today"));
                if (t.priority == Priority::Urgent || t.priority == Priority::High)
                    why.append(t.priority == Priority::Urgent ? QStringLiteral("Urgent") : QStringLiteral("High priority"));
                if (!where.isEmpty())
                    why.append(QStringLiteral("Demanding, so %1").arg(where));
            }
            for (const QString &p : graph.prerequisitesOf(t.id)) {
                if (placedEnd.contains(p))
                    why.append(QStringLiteral("After “%1”").arg(tasks.value(p).title));
            }

            plan.blocks.append({t.id, t.title, *start, *start + duration, why.join(QStringLiteral(" · "))});
            placedBusy.append({*start, *start + duration + breakMs});
            placedEnd.insert(t.id, *start + duration);
            progressed = true;
        }
        pending = waiting;
    }

    for (const Task &t : std::as_const(pending)) {
        bool blocked = false;
        for (const QString &p : graph.prerequisitesOf(t.id))
            blocked |= !isFinished(tasks.value(p).status) && !placedEnd.contains(p);
        QString reason;
        if (blocked)
            reason = QStringLiteral("Waiting on something that isn't done yet");
        else if (plan.blocks.size() >= options.maxBlocks)
            reason = QStringLiteral("Today is full enough");
        else
            reason = QStringLiteral("No room left in its category's hours today");
        plan.unplaced.append({t.id, t.title, reason});
    }

    std::sort(plan.blocks.begin(), plan.blocks.end(), [](const PlannedBlock &a, const PlannedBlock &b) { return a.start < b.start; });
    return plan;
}

} // namespace tumbler
