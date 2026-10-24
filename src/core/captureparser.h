#pragma once

#include "recurrence.h"
#include "types.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QTime>
#include <optional>

namespace tumbler {

// What quick capture understood from one line of text. Anything not
// recognised stays in the title, so nothing typed is ever lost.
struct CaptureResult {
    QString title;
    std::optional<Priority> priority;
    std::optional<Energy> energy;
    std::optional<int> estimateMinutes;
    std::optional<QDate> date;
    std::optional<QTime> time;
    std::optional<RecurrencePattern> recurrence;
    QStringList tags;
    QString category; // name after '/', resolved by the caller
};

// Offline, predictable parsing of a capture line. Recognised tokens:
//   #tag                       tag
//   /name                      category
//   !urgent !high !med !low    priority; also !!! (urgent) and !! (high)
//   +low +med +high +peak      energy
//   ~30m ~1h ~1h30m ~90        time estimate (bare numbers are minutes)
//   today tonight tomorrow     date; also full weekday names, "on fri",
//   "next tue", "next week",   "in N days" and YYYY-MM-DD
//   at 3pm, at 15:30, @9       time of day (needs "at" or "@")
//   daily, every day, every weekday, every weekend, every N days,
//   every mon[ and thu], every other tue, every N weeks on mon
//                              recurrence
// `today` anchors relative dates.
CaptureResult parseCapture(const QString &text, QDate today);

} // namespace tumbler
