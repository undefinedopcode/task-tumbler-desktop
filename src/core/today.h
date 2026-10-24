#pragma once

#include "entities.h"
#include "queries.h"

#include <QList>
#include <QTimeZone>
#include <optional>

namespace tumbler {

// One thing on today's agenda: a scheduled block, a recurring item, or an event.
struct AgendaItem {
    enum class Kind { Schedule, Occurrence, Event };
    enum class State { Upcoming, Now, Overdue, Done, Skipped };

    Kind kind = Kind::Schedule;
    State state = State::Upcoming;
    QString id;     // schedule / occurrence / event id
    QString taskId; // empty for events
    QString title;
    qint64 start = 0;
    qint64 end = 0;
    bool allDay = false;
    bool timeless = false; // recurring item with no time of day
    bool working = false;  // the running work session is on this task
    bool startsSoon = false;
    int bumpedCount = 0;
    QString color;
    Priority priority = Priority::Medium;
    Energy energy = Energy::Medium;
};

struct ReadyTask {
    Task task;
    int subtasksDone = 0;
    int subtasksTotal = 0;
};

struct TodayView {
    QDate date;
    std::optional<WorkLog> working;
    QString workingTitle;
    QList<AgendaItem> allDay; // all-day events
    QList<AgendaItem> now;
    QList<AgendaItem> upcoming;
    QList<AgendaItem> overdue;
    QList<AgendaItem> done;
    QList<ReadyTask> ready;
    int readyTotal = 0; // before the limit
};

// Builds the Today view, following the web dashboard's sections:
//  - blocks and recurring items are Now while their time window contains
//    `now`, Overdue once it has passed undone, Done when completed or skipped;
//  - recurring items without a time sit at 09:00 for ordering, but are never
//    marked overdue;
//  - Ready to Work: open one-off tasks with no open block from today on,
//    excluding tasks blocked by unfinished prerequisites and parents whose
//    subtasks are still open (the subtasks show instead). Sorted by priority,
//    then due date.
TodayView buildToday(const Queries &q, qint64 now, const QTimeZone &zone, int readyLimit = 8);

constexpr qint64 kStartsSoonMs = 15 * 60 * 1000;

} // namespace tumbler
