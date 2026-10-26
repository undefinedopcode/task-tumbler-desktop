#pragma once

#include "entities.h"
#include "planner.h"
#include "queries.h"
#include "stats.h"

#include <QHash>
#include <QList>
#include <QTimeZone>
#include <optional>

namespace tumbler {

// Guidance the planner can take from outside: an AI ranking, or the user.
// It only ever orders and filters tasks; times are always computed here.
struct PlanGuidance {
    QStringList order;              // task ids, most important first
    QHash<QString, QString> defer;  // task id -> why it should wait
    QHash<QString, QString> notes;  // task id -> a short reason to show
    QString explanation;            // overall summary to show
};

struct PlanOptions {
    QDate day;
    qint64 earliest = 0;            // nothing starts before this (usually now)
    std::optional<TimeWindow> peak; // best-energy hours
    int breakMinutes = 10;          // gap kept after each planned block
    // 1-10 from the latest check-in; nullopt when unknown. Low energy keeps
    // peak-energy tasks off today's plan and favours easy ones.
    std::optional<int> energyLevel;
    int maxBlocks = 12;
    std::optional<PlanGuidance> guidance;
    // Stretches estimates to how long things usually take you. Empty: plan
    // with the estimates as they are.
    stats::Pace pace;
};

struct PlannedBlock {
    QString taskId;
    QString title;
    qint64 start = 0;
    qint64 end = 0;
    QString reason;
    int estimateMinutes = 0; // the task's own estimate; the block may be longer (pace)
};

struct UnplacedTask {
    QString taskId;
    QString title;
    QString reason;
};

struct DayPlan {
    QDate day;
    QList<PlannedBlock> blocks; // in time order
    QList<UnplacedTask> unplaced;
    QString explanation;
};

// The tasks a plan for `day` may include: open one-off tasks with no open
// block from that day on, excluding parents with open steps (the steps are
// planned instead). Shared by the planner and the AI so both see the same set.
QList<Task> planCandidates(const Queries &q, QDate day, const QTimeZone &zone);

// Fills the free time on `options.day` with open tasks, deterministically:
//  - candidates are open one-off tasks with no open block from today on,
//    excluding parents with open steps (the steps are planned instead);
//  - a task waiting on unfinished prerequisites is only planned if every
//    prerequisite is itself planned earlier the same day;
//  - each block is the task's estimate, stretched by `options.pace`;
//  - each task goes in its own category's hours, around existing blocks,
//    timed recurring items and events that don't allow it, with a break
//    after each planned block;
//  - high/peak-energy tasks try the peak window first; low-energy tasks try
//    outside it first, leaving the best hours for hard work;
//  - ranking is the guidance order when given, otherwise in-progress first,
//    then overdue/due today, priority, due date, age.
// Existing blocks are never moved.
DayPlan planDay(const Queries &q, const PlanOptions &options, const QTimeZone &zone);

} // namespace tumbler
