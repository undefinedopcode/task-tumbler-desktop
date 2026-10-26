#pragma once

#include "queries.h"
#include "stats.h"

#include <QDate>
#include <QList>
#include <QTimeZone>

// Closing out a day and looking back over a week. Both lead with what got
// done, as a list rather than a score, then offer one tap per loose end.
// Read-only: the changes are the app's usual scheduling actions.
namespace tumbler::review {

struct Win {
    QString taskId;
    QString title;
    qint64 at = 0;
    bool routine = false;
    int workedMinutes = 0; // all time logged on the task
};

// Worked on in the range, still open.
struct Progress {
    QString taskId;
    QString title;
    int minutes = 0;
};

// Something meant for today (or a recent day) that didn't happen.
struct Leftover {
    enum class Kind { Block, Routine };
    Kind kind = Kind::Block;
    QString id; // schedule or occurrence id
    QString taskId;
    QString title;
    qint64 start = 0;
    qint64 end = 0;
    bool timeless = false; // routine without a time of day
    bool earlier = false;  // from a day before this one
    int bumps = 0;
};

struct Tomorrow {
    QDate date;
    int blocks = 0;
    int routines = 0;
    int events = 0; // timed and all-day
    QString firstTitle; // the first timed thing, if any
    qint64 firstAt = 0;
};

struct DayReview {
    QDate date;
    int workedMinutes = 0;
    QList<Win> wins;           // in the order they were finished
    QList<Progress> progress;  // most worked first
    QList<Leftover> leftovers; // earliest first
    Tomorrow tomorrow;
};

// Leftovers are open blocks from `lookbackDays` before `day` up to its end
// whose task is still open, and the day's routines not done or skipped.
DayReview reviewDay(const Queries &q, QDate day, const QTimeZone &zone, int lookbackDays = 14);

// An open task nobody has touched in a while.
struct Stale {
    QString taskId;
    QString title;
    int days = 0; // since it was last changed or worked on
};

struct DueSoon {
    QString taskId;
    QString title;
    QDate due;
};

struct WeekReview {
    stats::Summary summary; // the numbers, the days, and what keeps moving
    QList<Win> wins;
    QList<Progress> progress;
    // Open one-off tasks with no block coming up, untouched for three weeks
    // or more: oldest first, at most five.
    QList<Stale> stale;
    // Open tasks due by a week after `to` (or already past due) with no block
    // coming up, soonest first.
    QList<DueSoon> dueSoon;
};

constexpr int kStaleDays = 21;

WeekReview reviewWeek(const Queries &q, QDate from, QDate to, const QTimeZone &zone);

} // namespace tumbler::review
