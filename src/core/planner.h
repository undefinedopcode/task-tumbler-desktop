#pragma once

#include "entities.h"
#include "queries.h"

#include <QList>
#include <QTimeZone>
#include <optional>

namespace tumbler {

struct Interval {
    qint64 start = 0;
    qint64 end = 0;
    bool operator==(const Interval &) const = default;
};

// Sorts and merges overlapping or touching intervals.
QList<Interval> mergeIntervals(QList<Interval> intervals);

// Earliest start >= `earliest`, on the `snapMs` grid, where
// [start, start + duration) lies inside one window and clear of every busy
// interval. Deterministic and offline: this is the piece of scheduling the
// AI is never trusted to do.
std::optional<qint64> findSlot(const QList<Interval> &windows, const QList<Interval> &busy, qint64 earliest, qint64 durationMs,
                               qint64 snapMs = 5 * 60 * 1000);

// A category's working windows as instants over `days` days from `firstDay`.
QList<Interval> categoryWindows(const CategorySchedule &schedule, QDate firstDay, int days, const QTimeZone &zone);

// Next free slot for a task within its category's hours, avoiding scheduled
// blocks, timed recurring items and events that do not allow the task.
// `ignoreScheduleId` leaves one block out, so a block can be moved.
std::optional<Interval> nextFreeSlot(const Queries &q, const Task &task, qint64 earliest, const QTimeZone &zone,
                                     int searchDays = 14, const QString &ignoreScheduleId = {});

} // namespace tumbler
