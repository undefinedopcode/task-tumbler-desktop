#pragma once

#include "queries.h"

#include <QDate>
#include <QHash>
#include <QString>
#include <QTimeZone>
#include <array>
#include <optional>

// Looking back: where the time went, what got finished, and how estimates
// compare with what things actually take. Read-only, over finished work
// sessions and the task history.
//
// Two changes from the web app's analytics:
// - Estimate accuracy only counts tasks that are finished, using all the
//   time logged on them. A half-done task isn't "under estimate".
// - The typical ratio is the median, so one task that ran for a day doesn't
//   swamp twenty that took what you thought.
namespace tumbler::stats {

struct Day {
    QDate date;
    int workedMinutes = 0;
    int finished = 0; // tasks and routine occurrences
};

struct TaskTime {
    QString taskId;
    QString title;
    int estimateMinutes = 0;
    int workedMinutes = 0; // in the range
    int sessions = 0;      // started in the range
    bool finished = false;
};

// How long finished tasks took against their estimates.
struct Calibration {
    QString label;
    int tasks = 0;
    double ratio = 0; // median of worked / estimate; 1.0 is spot on
    int estimatedMinutes = 0;
    int workedMinutes = 0;
};

// Open tasks that keep being bumped to later.
struct Moving {
    QString taskId;
    QString title;
    int bumps = 0;
};

struct Summary {
    QDate from;
    QDate to; // inclusive
    QList<Day> days;
    int workedMinutes = 0;
    int sessions = 0;
    int longestSessionMinutes = 0;
    int finishedTasks = 0;
    int finishedRoutines = 0;
    std::array<int, 24> minutesByHour{};                 // local hour of day
    std::array<std::optional<double>, 24> energyByHour{}; // average check-in, 1-10
    QList<TaskTime> tasks;                                // most worked first
    Calibration overall;
    QList<Calibration> bySize;     // estimate buckets, shortest first; empty buckets left out
    QList<Calibration> byCategory; // most tasks first
    QList<Moving> moving;          // bumped twice or more in the range, most first, at most five
};

// The first day with any work, completion or check-in, if there is one.
std::optional<QDate> firstActivity(const Queries &q, const QTimeZone &zone);

Summary summarize(const Queries &q, QDate from, QDate to, const QTimeZone &zone);

// How long things take you compared with your estimates, for planning:
// learned from finished tasks, by category, then by estimate size, then
// overall, whichever is most specific with enough tasks behind it.
// It only ever lengthens a plan. Shortening risks an overfull day, and
// finishing early is a nice surprise; running over is the one that hurts.
class Pace
{
public:
    static constexpr int kMinTasks = 5;      // finished tasks with time logged before anything changes
    static constexpr int kMinGroupTasks = 3; // for a category or size of its own
    static constexpr double kMinRatio = 1.1; // closer than this counts as spot on
    static constexpr double kMaxRatio = 2.0; // never more than double an estimate

    struct Factor {
        double ratio = 1.0;
        int tasks = 0;
        QString basis; // "category", "size" or "overall"
    };

    bool isEmpty() const { return !m_overall; }
    std::optional<Factor> factorFor(int estimate, const QString &categoryId) const;
    // The estimate stretched to your pace, rounded up to 5 minutes; the
    // estimate itself when there's nothing to go on or it's close enough.
    int plannedMinutes(int estimate, const QString &categoryId) const;

private:
    friend Pace learnPace(const Queries &, qint64, qint64);
    std::optional<Factor> m_overall;
    QHash<QString, Factor> m_bySize;     // sizeOf key
    QHash<QString, Factor> m_byCategory; // category id
};

// From tasks finished in [since, until).
Pace learnPace(const Queries &q, qint64 since, qint64 until);

} // namespace tumbler::stats
