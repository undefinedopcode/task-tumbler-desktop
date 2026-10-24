#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTimeZone>

namespace tumbler {

class Database;

struct ImportReport {
    QString sourceKind; // "postgres" or "sqlite"
    QString userEmail;
    QString timezone;   // zone naive local timestamps were read in
    QMap<QString, int> imported;
    QMap<QString, int> skipped;
    QStringList warnings;

    QString summary() const;
};

// Loads a web-app export (server/src/scripts/export-desktop.ts, format
// "task-tumbler-export" v1) into a migrated, empty database, all in one
// transaction: either everything lands or nothing does.
//
// How the web app's timestamps are read:
//  - schedules.scheduled_* / actual_*: naive values were written as local
//    time, so they are read in the user's timezone preference.
//  - events: timed events hold wall-clock times in their own timezone column
//    (Australia/Sydney when empty).
//  - everything else naive (created_at, history, logs) is UTC.
//  - values carrying Z or an offset are exact regardless of column.
class Importer
{
public:
    explicit Importer(Database &db);

    // Allow importing into a database that already has tasks. Off by default
    // so a second import cannot silently duplicate everything.
    void setAllowNonEmpty(bool allow) { m_allowNonEmpty = allow; }

    // Zone for naive local timestamps when the export has no timezone
    // preference. Defaults to the system zone.
    void setFallbackTimeZone(const QTimeZone &zone) { m_fallbackZone = zone; }

    bool run(const QJsonObject &doc, ImportReport *report, QString *error = nullptr);

    static constexpr int kSupportedVersion = 1;

private:
    Database &m_db;
    bool m_allowNonEmpty = false;
    QTimeZone m_fallbackZone = QTimeZone::systemTimeZone();
};

} // namespace tumbler
