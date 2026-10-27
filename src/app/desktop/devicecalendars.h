#pragma once

#include "icsfeed.h"

#include <QDate>
#include <QList>
#include <QString>
#include <functional>
#include <optional>

class QObject;
class QDateTime;

namespace tumbler::devicecalendars {

// The calendars already set up on this computer: on macOS everything in
// Calendar.app (iCloud, Google, Exchange, subscriptions) through EventKit,
// after the user allows it once. Elsewhere, unsupported for now.
// devicecalendars_mac.mm / devicecalendars_linux.cpp; the rest is shared.
//
// One of them, picked in Settings, is a calendar feed like any other whose
// address is "device:<calendar id>", so it's listed, coloured, blocks time
// and is removed the same way.

bool supported();

enum class Access { NotAsked, Granted, Denied };
Access access();
// Asks once (the system prompt); `done` runs later on `context`'s thread.
void requestAccess(QObject *context, std::function<void(bool granted)> done);

struct Calendar {
    QString id;
    QString title;
    QString account; // "iCloud", "Exchange", an email address…
    QString color;   // #rrggbb
};
QList<Calendar> calendars();

// One occurrence of an event; repeats come expanded.
struct Occurrence {
    QString id; // stable for this occurrence across reads
    QString title;
    QString location;
    QString notes;
    bool allDay = false;
    qint64 start = 0; // timed: UTC ms
    qint64 end = 0;
    QDate startDate;  // all-day: end inclusive
    QDate endDate;
    bool free = false; // "show as free"
};
// Occurrences overlapping [from, to); cancelled ones left out.
QList<Occurrence> occurrences(const QString &calendarId, const QDateTime &from, const QDateTime &to, QString *error = nullptr);

// `changed` runs on `context`'s thread whenever the calendars change.
void watch(QObject *context, std::function<void()> changed);

// ---- shared --------------------------------------------------------------------
QString feedUrl(const QString &calendarId);
std::optional<QString> calendarOf(const QString &feedUrl);
QList<sync::RemoteEvent> toRemoteEvents(const QList<Occurrence> &occurrences, const sync::FeedSettings &feed);

} // namespace tumbler::devicecalendars
