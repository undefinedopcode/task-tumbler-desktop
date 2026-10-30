#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>
#include <optional>

namespace tumbler {

// Time worked on something ticked off without pressing Start: from when it
// was due to start until it was ticked, if that's about what was planned.
struct WorkGuess {
    qint64 start = 0;
    qint64 end = 0;
    int plannedMinutes = 0;
    bool sure = false; // false: much longer than planned; ask before logging
};

// `start` is when it was planned to start, `plannedMinutes` how long it was
// meant to take. Nullopt when it isn't due yet (or only just). Sure when the
// time since start is within half again the plan, or 15 minutes over.
std::optional<WorkGuess> guessWork(qint64 start, int plannedMinutes, qint64 now);

// A guess that wasn't sure, waiting for "how long did it actually take?".
// Kept in the time_checks setting until answered or a day and a half old.
struct TimeCheck {
    QString id;
    QString taskId;
    QString title;
    QString blockId; // the schedule block it was ticked off on, if any
    qint64 start = 0;
    qint64 doneAt = 0;
    int plannedMinutes = 0;
};

QJsonArray toJson(const QList<TimeCheck> &checks);
QList<TimeCheck> timeChecksFromJson(const QJsonArray &array);
// Drops checks too old to remember the answer to.
QList<TimeCheck> freshChecks(QList<TimeCheck> checks, qint64 now);

// Where `minutes` of work go: from the planned start, or ending when it was
// ticked off if that's longer than the time in between.
std::pair<qint64, qint64> workSpan(const TimeCheck &check, int minutes);

} // namespace tumbler
