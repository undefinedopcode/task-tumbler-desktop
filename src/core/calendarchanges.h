#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>
#include <optional>

namespace tumbler {

// A change a calendar sync brought to an event that hasn't happened yet:
// someone added a meeting, moved it, cancelled it or changed the room. Kept
// (in the calendar_changes setting) until the user dismisses it or the event
// is over, so a change made overnight is still there in the morning.
struct CalendarChange {
    enum class Kind { Added, Moved, Cancelled, Changed };
    QString key;     // the feed's id for the event (one entry per event)
    Kind kind = Kind::Added;
    QString eventId; // empty once cancelled
    QString title;
    QString location;
    QString oldLocation;
    qint64 start = 0; // as it is now (cancelled: as it was)
    qint64 end = 0;
    qint64 oldStart = 0; // before a move
    qint64 oldEnd = 0;
    qint64 seenAt = 0;

    // The latest the change matters until.
    qint64 over() const { return std::max(end, oldEnd); }
    // The earliest time it affects: the new time, or the old one if sooner.
    qint64 affects() const { return kind == Kind::Moved && oldStart > 0 ? std::min(start, oldStart) : start; }
};

QString nameOf(CalendarChange::Kind kind);
std::optional<CalendarChange::Kind> changeKindFromString(const QString &s);

QJsonArray toJson(const QList<CalendarChange> &changes);
QList<CalendarChange> changesFromJson(const QJsonArray &array);

// Folds fresh changes into the kept ones, one entry per event: a new meeting
// that then moves is still "new", at its new time; a move back to where it
// was is no change at all; a cancellation replaces whatever came before.
// Changes whose event is over by `now` are dropped.
QList<CalendarChange> mergeChanges(QList<CalendarChange> kept, const QList<CalendarChange> &fresh, qint64 now);

// How hard to get the user's attention, by how soon the change bites.
enum class ChangeUrgency {
    Later, // shown on Today only
    Today, // within two hours: a notification and one bounce
    Soon,  // within 30 minutes, or under way: urgent, bounce until seen
};
ChangeUrgency urgencyOf(const CalendarChange &change, qint64 now);
QString nameOf(ChangeUrgency urgency);

} // namespace tumbler
