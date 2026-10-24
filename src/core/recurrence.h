#pragma once

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <optional>

namespace tumbler {

// A task's repeat rule, stored as JSON in tasks.recurrence. The JSON shape is
// the web app's RecurrencePattern so exported data imports unchanged:
//   {"type":"daily"|"weekly","interval":N,"days":[0..6],"startDate":"YYYY-MM-DD","endDate":"YYYY-MM-DD"}
//
// Semantics (all dates are local calendar dates):
//  - Nothing occurs before startDate or after endDate.
//  - daily:  every `interval` days counted from startDate.
//  - weekly: on the listed weekdays (0 = Sunday) of every `interval`-th week,
//            where weeks run Sunday..Saturday and week 0 is the one holding
//            startDate. Days of week 0 before startDate are skipped.
//
// The web app's generator anchored on the requested range start when
// startDate was missing, so interval > 1 drifted depending on when it ran, and
// it skipped the rest of the first week when the anchor fell mid-week. Here
// startDate is required for evaluation; the importer fills it in.
struct RecurrencePattern {
    enum class Type { Daily, Weekly };

    Type type = Type::Daily;
    int interval = 1;
    QList<int> days; // weekly only, 0 = Sunday
    QDate startDate;
    std::optional<QDate> endDate;

    static std::optional<RecurrencePattern> fromJson(const QJsonObject &json, QString *error = nullptr);
    QJsonObject toJson() const;

    bool isValid(QString *error = nullptr) const;
    bool occursOn(QDate date) const;

    // Every occurrence in [from, to], inclusive and ascending.
    QList<QDate> occurrencesBetween(QDate from, QDate to) const;

    bool operator==(const RecurrencePattern &) const = default;
};

} // namespace tumbler
