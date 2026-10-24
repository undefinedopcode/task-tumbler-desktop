#include "today.h"

#include "dependencygraph.h"

#include <QDateTime>
#include <algorithm>

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;

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

void classify(AgendaItem &item, qint64 now, bool finished)
{
    if (item.state == AgendaItem::State::Skipped)
        return;
    if (finished)
        item.state = AgendaItem::State::Done;
    else if (item.start <= now && now < item.end)
        item.state = AgendaItem::State::Now;
    else if (item.start > now) {
        item.state = AgendaItem::State::Upcoming;
        item.startsSoon = item.start - now <= kStartsSoonMs;
    } else
        item.state = item.timeless ? AgendaItem::State::Upcoming : AgendaItem::State::Overdue;
}
} // namespace

TodayView buildToday(const Queries &q, qint64 now, const QTimeZone &zone, int readyLimit)
{
    TodayView view;
    view.date = QDateTime::fromMSecsSinceEpoch(now, zone).date();
    const qint64 dayStart = QDateTime(view.date, QTime(0, 0), zone).toMSecsSinceEpoch();
    const qint64 dayEnd = QDateTime(view.date.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();

    QHash<QString, Task> tasks;
    for (const Task &t : q.tasks())
        tasks.insert(t.id, t);

    view.working = q.activeWorkLog();
    if (view.working)
        view.workingTitle = tasks.value(view.working->taskId).title;
    const QString workingTask = view.working ? view.working->taskId : QString();

    QList<AgendaItem> items;

    for (const ScheduleBlock &s : q.schedules(dayStart, dayEnd)) {
        const Task t = tasks.value(s.taskId);
        AgendaItem item;
        item.kind = AgendaItem::Kind::Schedule;
        item.id = s.id;
        item.taskId = s.taskId;
        item.title = t.title.isEmpty() ? QStringLiteral("(deleted task)") : t.title;
        item.start = s.start;
        item.end = s.end;
        item.bumpedCount = s.bumpedCount;
        item.color = t.color;
        item.priority = t.priority;
        item.energy = t.energy;
        item.working = !workingTask.isEmpty() && workingTask == s.taskId;
        classify(item, now, s.completed);
        items.append(item);
    }

    for (const Occurrence &o : q.occurrences(view.date, view.date)) {
        const auto it = tasks.constFind(o.taskId);
        if (it == tasks.constEnd())
            continue;
        const QTime time = o.scheduledTime.isValid() ? o.scheduledTime : it->scheduledTime;
        AgendaItem item;
        item.kind = AgendaItem::Kind::Occurrence;
        item.id = o.id;
        item.taskId = o.taskId;
        item.title = it->title;
        item.timeless = !time.isValid();
        item.start = QDateTime(view.date, time.isValid() ? time : QTime(9, 0), zone).toMSecsSinceEpoch();
        item.end = item.start + qint64(std::max(5, it->estimateMinutes)) * kMinute;
        item.color = it->color;
        item.priority = it->priority;
        item.energy = it->energy;
        item.working = !workingTask.isEmpty() && workingTask == o.taskId;
        if (o.status == OccurrenceStatus::Skipped)
            item.state = AgendaItem::State::Skipped;
        classify(item, now, o.status == OccurrenceStatus::Completed);
        items.append(item);
    }

    for (const Event &e : q.events(dayStart, dayEnd, view.date, view.date)) {
        AgendaItem item;
        item.kind = AgendaItem::Kind::Event;
        item.id = e.id;
        item.title = e.title;
        item.color = e.color;
        item.allDay = e.allDay;
        if (e.allDay) {
            item.start = dayStart;
            item.end = dayEnd;
            view.allDay.append(item);
            continue;
        }
        item.start = e.startAt;
        item.end = e.endAt;
        // Events are never "overdue": once over they are simply done.
        classify(item, now, e.endAt <= now);
        items.append(item);
    }

    std::sort(items.begin(), items.end(), [](const AgendaItem &a, const AgendaItem &b) {
        return a.start != b.start ? a.start < b.start : a.title < b.title;
    });
    for (const AgendaItem &item : items) {
        switch (item.state) {
        case AgendaItem::State::Now:
            view.now.append(item);
            break;
        case AgendaItem::State::Upcoming:
            view.upcoming.append(item);
            break;
        case AgendaItem::State::Overdue:
            view.overdue.append(item);
            break;
        case AgendaItem::State::Done:
        case AgendaItem::State::Skipped:
            view.done.append(item);
            break;
        }
    }

    // Ready to Work.
    QSet<QString> planned;
    for (const ScheduleBlock &s : q.schedules(dayStart, std::numeric_limits<qint64>::max())) {
        if (!s.completed)
            planned.insert(s.taskId);
    }
    DependencyGraph graph;
    for (const Dependency &d : q.dependencies())
        graph.add(d.taskId, d.dependsOnId);
    const auto statusOf = [&tasks](const QString &id) { return tasks.value(id).status; };

    QHash<QString, QPair<int, int>> subtaskCounts; // done, total
    for (const Task &t : std::as_const(tasks)) {
        if (t.parentId.isEmpty())
            continue;
        auto &c = subtaskCounts[t.parentId];
        c.second++;
        if (isFinished(t.status))
            c.first++;
    }

    QList<ReadyTask> ready;
    for (const Task &t : std::as_const(tasks)) {
        if (t.isRecurring() || isFinished(t.status) || planned.contains(t.id))
            continue;
        if (!graph.blockers(t.id, statusOf).isEmpty())
            continue;
        const auto counts = subtaskCounts.value(t.id);
        if (counts.second > counts.first)
            continue;
        ready.append({t, counts.first, counts.second});
    }
    std::sort(ready.begin(), ready.end(), [](const ReadyTask &a, const ReadyTask &b) {
        // In-progress work first: it is the easiest thing to pick back up.
        const bool aStarted = a.task.status == TaskStatus::InProgress;
        const bool bStarted = b.task.status == TaskStatus::InProgress;
        if (aStarted != bStarted)
            return aStarted;
        if (a.task.priority != b.task.priority)
            return priorityRank(a.task.priority) < priorityRank(b.task.priority);
        if (a.task.dueDate.isValid() != b.task.dueDate.isValid())
            return a.task.dueDate.isValid();
        if (a.task.dueDate != b.task.dueDate)
            return a.task.dueDate < b.task.dueDate;
        return a.task.createdAt < b.task.createdAt;
    });
    view.readyTotal = ready.size();
    view.ready = ready.mid(0, readyLimit);
    return view;
}

} // namespace tumbler
