#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTimeZone>
#include <optional>

namespace tumbler {

// Reading the timestamp text the web app left behind. It holds every shape
// SQLite and Postgres produced over its life:
//   2025-11-22T09:47:30.188Z        JS toISOString()
//   2025-12-03T12:15:00             naive, written as local time
//   2025-12-03 18:41:30             SQLite datetime('now'), naive UTC
//   2026-10-24 19:56:20.524079      Postgres TIMESTAMP, naive
//   2026-03-01 10:00:00+00          Postgres TIMESTAMPTZ
// What a naive value means depends on the column, so the caller supplies the
// zone to read it in.
struct ParsedTimestamp {
    QDateTime utc;
    bool hadOffset = false; // false: interpreted in the caller's naive zone
};

std::optional<ParsedTimestamp> parseTimestamp(const QString &text, const QTimeZone &naiveZone);

// Milliseconds since the Unix epoch, the storage format for instants.
std::optional<qint64> parseTimestampMs(const QString &text, const QTimeZone &naiveZone);

// A calendar date column. Plain "YYYY-MM-DD" is taken as is. A value with an
// explicit offset ("2025-11-19T07:00:00.000Z") is an instant, and its date is
// read in `zone`, so a local midnight stored as UTC lands on the right day.
// A naive date-time keeps its own date part.
QDate parseLocalDate(const QString &text, const QTimeZone &zone);

} // namespace tumbler
