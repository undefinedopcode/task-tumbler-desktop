#pragma once

#include "today.h"

#include <QList>
#include <QSet>
#include <QString>

namespace tumbler {

struct Reminder {
    enum class Kind { StartingSoon, StartingNow, WrapUp, Missed };
    Kind kind;
    AgendaItem item;
    QString key; // fired-once key
};

// Which reminders are due at `now`, given what has already fired. Pure, so the
// timing rules are testable without a notification server:
//  - StartingSoon: `leadMs` before a timed block / recurring item / event, as
//    long as we are not already past the lead window by more than the grace;
//  - StartingNow: at the start (within the grace), unless already working on it;
//  - WrapUp: five minutes before the end of the block being worked on;
//  - Missed: a block that ended undone within the grace, never for blocks
//    that ended before `startedAt` (app launch), so starting up doesn't
//    announce the whole morning.
// Keys of returned reminders are added to `fired`.
QList<Reminder> dueReminders(const TodayView &view, qint64 now, qint64 leadMs, qint64 startedAt, QSet<QString> &fired);

constexpr qint64 kReminderGraceMs = 2 * 60 * 1000;

} // namespace tumbler
