#include "importer.h"

#include "blocking.h"
#include "categoryschedule.h"
#include "database.h"
#include "dependencygraph.h"
#include "recurrence.h"
#include "timeparse.h"
#include "types.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariantMap>
#include <optional>

namespace tumbler {

QString ImportReport::summary() const
{
    QStringList parts;
    for (auto it = imported.cbegin(); it != imported.cend(); ++it) {
        QString part = QStringLiteral("%1=%2").arg(it.key()).arg(it.value());
        if (const int s = skipped.value(it.key()); s > 0)
            part += QStringLiteral(" (skipped %1)").arg(s);
        parts.append(part);
    }
    return parts.join(QLatin1Char(' '));
}

namespace {

const QTimeZone kUtc = QTimeZone::UTC;

// ---- Reading loosely typed JSON values ---------------------------------------
// The export carries raw database values: SQLite gives 0/1 and JSON-as-text,
// Postgres gives real booleans and JSONB objects.

QString str(const QJsonObject &row, const char *key)
{
    const QJsonValue v = row.value(QLatin1String(key));
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toDouble(), 'g', 15);
    return {};
}

QVariant optStr(const QJsonObject &row, const char *key)
{
    const QString s = str(row, key);
    return s.isEmpty() ? QVariant() : QVariant(s);
}

bool boolean(const QJsonObject &row, const char *key)
{
    const QJsonValue v = row.value(QLatin1String(key));
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() != 0;
    const QString s = v.toString().toLower();
    return s == QLatin1String("true") || s == QLatin1String("t") || s == QLatin1String("1");
}

std::optional<double> number(const QJsonObject &row, const char *key)
{
    const QJsonValue v = row.value(QLatin1String(key));
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return d;
    }
    return std::nullopt;
}

QVariant optInt(const QJsonObject &row, const char *key)
{
    const auto n = number(row, key);
    return n ? QVariant(qint64(*n)) : QVariant();
}

// A JSON column as a value: objects and arrays pass through, strings holding
// JSON are parsed, anything else is null.
QJsonValue jsonColumn(const QJsonObject &row, const char *key)
{
    const QJsonValue v = row.value(QLatin1String(key));
    if (v.isObject() || v.isArray())
        return v;
    if (v.isString() && !v.toString().isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (doc.isObject())
            return doc.object();
        if (doc.isArray())
            return doc.array();
        return v; // plain text where JSON was expected; keep it as a JSON string
    }
    return QJsonValue::Null;
}

QString jsonText(const QJsonValue &v)
{
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    // Scalars: wrap in an array to serialise, then strip the brackets.
    const QByteArray wrapped = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

QVariant optJsonText(const QJsonObject &row, const char *key)
{
    const QJsonValue v = jsonColumn(row, key);
    return v.isNull() ? QVariant() : QVariant(jsonText(v));
}

QString tagsText(const QJsonObject &row, const char *key)
{
    QJsonArray tags;
    for (const QJsonValue &t : jsonColumn(row, key).toArray()) {
        const QString s = t.toString().trimmed();
        if (!s.isEmpty())
            tags.append(s);
    }
    return jsonText(tags);
}

QVariant hhmm(const QString &s)
{
    static const QRegularExpression re(QStringLiteral(R"(^([01]?\d|2[0-3]):([0-5]\d)(:\d{2})?$)"));
    const auto m = re.match(s.trimmed());
    if (!m.hasMatch())
        return {};
    return QStringLiteral("%1:%2").arg(m.captured(1).toInt(), 2, 10, QLatin1Char('0')).arg(m.captured(2));
}

// Occurrence dates are read the way the web app read them back: the leading
// YYYY-MM-DD, whatever time or zone suffix follows.
QDate leadingDate(const QString &text)
{
    return QDate::fromString(text.trimmed().left(10), Qt::ISODate);
}

QJsonArray rows(const QJsonObject &tables, const char *name)
{
    return tables.value(QLatin1String(name)).toArray();
}

// ---- One import run ---------------------------------------------------------------

class Run
{
public:
    Run(QSqlDatabase db, ImportReport &report, QTimeZone zone, qint64 now)
        : m_db(db), m_report(report), m_zone(zone), m_now(now)
    {
    }

    bool importAll(const QJsonObject &doc, QString *error);

private:
    bool insert(const char *table, const QVariantMap &values);
    void skip(const char *table, const QString &why);
    void warn(const QString &message) { m_report.warnings.append(message); }

    qint64 utcMs(const QJsonObject &row, const char *key, bool required = true);
    QVariant optUtcMs(const QJsonObject &row, const char *key);
    QVariant optLocalMs(const QJsonObject &row, const char *key);

    bool settings(const QJsonObject &prefs);
    bool categories(const QJsonArray &list, const QJsonObject &prefs);
    bool tasks(const QJsonArray &list, const QJsonArray &occurrences);
    bool dependencies(const QJsonArray &list);
    bool occurrences(const QJsonArray &list);
    bool workLogs(const QJsonArray &list);
    bool schedules(const QJsonArray &list);
    bool events(const QJsonArray &list);
    bool energyLogs(const QJsonArray &list);
    bool healthLogs(const QJsonArray &list);
    bool healthSummaries(const QJsonArray &list);
    bool taskHistory(const QJsonArray &list);
    bool aiLogs(const QJsonArray &list);
    bool notes(const QJsonArray &list);
    bool integrations(const QJsonArray &list);
    bool syncedItems(const QJsonArray &list);
    bool syncedEvents(const QJsonArray &list);

    QSqlDatabase m_db;
    ImportReport &m_report;
    QTimeZone m_zone;
    qint64 m_now;
    QString m_lastError;

    QSet<QString> m_categories;
    QString m_defaultCategory;
    QHash<QString, TaskStatus> m_tasks;
    QSet<QString> m_workLogs;
    QSet<QString> m_events;
    QSet<QString> m_integrations;
};

bool Run::insert(const char *table, const QVariantMap &values)
{
    QStringList columns, placeholders;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        columns.append(it.key());
        placeholders.append(QStringLiteral("?"));
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                  .arg(QLatin1String(table), columns.join(QLatin1Char(',')), placeholders.join(QLatin1Char(','))));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        q.addBindValue(it.value());
    if (!q.exec()) {
        m_lastError = QStringLiteral("%1 %2: %3").arg(QLatin1String(table), values.value(QStringLiteral("id")).toString(),
                                                     q.lastError().text());
        return false;
    }
    ++m_report.imported[QLatin1String(table)];
    return true;
}

void Run::skip(const char *table, const QString &why)
{
    ++m_report.skipped[QLatin1String(table)];
    warn(QStringLiteral("%1: %2").arg(QLatin1String(table), why));
}

qint64 Run::utcMs(const QJsonObject &row, const char *key, bool required)
{
    const QString text = str(row, key);
    if (const auto ms = parseTimestampMs(text, kUtc))
        return *ms;
    if (required && !text.isEmpty())
        warn(QStringLiteral("unreadable timestamp '%1' in %2; using import time").arg(text, QLatin1String(key)));
    return m_now;
}

QVariant Run::optUtcMs(const QJsonObject &row, const char *key)
{
    const auto ms = parseTimestampMs(str(row, key), kUtc);
    return ms ? QVariant(*ms) : QVariant();
}

QVariant Run::optLocalMs(const QJsonObject &row, const char *key)
{
    const auto ms = parseTimestampMs(str(row, key), m_zone);
    return ms ? QVariant(*ms) : QVariant();
}

bool Run::importAll(const QJsonObject &doc, QString *error)
{
    const QJsonObject tables = doc.value(QLatin1String("tables")).toObject();
    const QJsonObject prefs = doc.value(QLatin1String("preferences")).toObject();

    const bool ok = settings(prefs)
        && categories(rows(tables, "categories"), prefs)
        && tasks(rows(tables, "tasks"), rows(tables, "task_occurrences"))
        && dependencies(rows(tables, "task_dependencies"))
        && occurrences(rows(tables, "task_occurrences"))
        && workLogs(rows(tables, "work_logs"))
        && schedules(rows(tables, "schedules"))
        && events(rows(tables, "events"))
        && energyLogs(rows(tables, "energy_logs"))
        && healthLogs(rows(tables, "health_logs"))
        && healthSummaries(rows(tables, "health_summaries"))
        && taskHistory(rows(tables, "task_history"))
        && aiLogs(rows(tables, "ai_logs"))
        && notes(rows(tables, "notes"))
        && integrations(rows(tables, "integrations"))
        && syncedItems(rows(tables, "synced_items"))
        && syncedEvents(rows(tables, "synced_events"));

    if (!ok && error)
        *error = m_lastError;
    return ok;
}

bool Run::settings(const QJsonObject &prefs)
{
    // Preferences become individual settings; values are stored as JSON.
    static const char *const keys[] = {
        "timezone", "peak_energy_start", "peak_energy_end", "work_start", "work_end",
        "break_duration_minutes", "ai_scheduling_hints", "background_sync_enabled",
        "background_sync_interval_minutes",
    };
    for (const char *key : keys) {
        const QJsonValue v = prefs.value(QLatin1String(key));
        if (v.isNull() || v.isUndefined())
            continue;
        if (!insert("settings", {{QStringLiteral("key"), QLatin1String(key)}, {QStringLiteral("value"), jsonText(v)}}))
            return false;
    }
    return true;
}

bool Run::categories(const QJsonArray &list, const QJsonObject &prefs)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString id = str(row, "id");
        const QString name = str(row, "name").trimmed();
        if (id.isEmpty() || name.isEmpty()) {
            skip("categories", QStringLiteral("row without id or name"));
            continue;
        }
        // Only one default survives; the schema enforces it.
        const bool isDefault = boolean(row, "is_default") && m_defaultCategory.isEmpty();
        const CategorySchedule schedule = CategorySchedule::fromJson(jsonColumn(row, "schedule").toObject());
        if (!insert("categories", {
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), name},
                {QStringLiteral("schedule"), jsonText(schedule.toJson())},
                {QStringLiteral("color"), str(row, "color").isEmpty() ? QStringLiteral("#6366f1") : str(row, "color")},
                {QStringLiteral("is_default"), isDefault ? 1 : 0},
                {QStringLiteral("show_on_schedule"), boolean(row, "show_on_schedule") ? 1 : 0},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
                {QStringLiteral("updated_at"), utcMs(row, "updated_at")},
            }))
            return false;
        m_categories.insert(id);
        if (isDefault)
            m_defaultCategory = id;
    }

    if (!m_defaultCategory.isEmpty())
        return true;

    // Exports from before categories existed get the same default category the
    // web app's migration 014 created: Mon-Fri between the work hours.
    const QTime start = QTime::fromString(prefs.value(QLatin1String("work_start")).toString(QStringLiteral("08:00")),
                                          QStringLiteral("HH:mm"));
    const QTime end = QTime::fromString(prefs.value(QLatin1String("work_end")).toString(QStringLiteral("18:00")),
                                        QStringLiteral("HH:mm"));
    const CategorySchedule schedule = (start.isValid() && end.isValid() && start < end)
        ? CategorySchedule::weekdays(start, end)
        : CategorySchedule::weekdays(QTime(8, 0), QTime(18, 0));

    QString name = QStringLiteral("Default");
    for (int n = 2; [&] {
             QSqlQuery q(m_db);
             q.prepare(QStringLiteral("SELECT 1 FROM categories WHERE name = ?"));
             q.addBindValue(name);
             return q.exec() && q.next();
         }();
         ++n)
        name = QStringLiteral("Default %1").arg(n);

    m_defaultCategory = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_categories.insert(m_defaultCategory);
    return insert("categories", {
        {QStringLiteral("id"), m_defaultCategory},
        {QStringLiteral("name"), name},
        {QStringLiteral("schedule"), jsonText(schedule.toJson())},
        {QStringLiteral("is_default"), 1},
        {QStringLiteral("created_at"), m_now},
        {QStringLiteral("updated_at"), m_now},
    });
}

bool Run::tasks(const QJsonArray &list, const QJsonArray &occurrenceRows)
{
    // First and last generated occurrence per task. The first is the best
    // anchor for a recurrence that never recorded its startDate; the last pins
    // which weeks an every-N-weeks task falls on.
    QHash<QString, QDate> firstOccurrence, lastOccurrence;
    for (const QJsonValue &v : occurrenceRows) {
        const QJsonObject row = v.toObject();
        const QDate d = leadingDate(str(row, "occurrence_date"));
        if (!d.isValid())
            continue;
        QDate &first = firstOccurrence[str(row, "task_id")];
        QDate &last = lastOccurrence[str(row, "task_id")];
        if (!first.isValid() || d < first)
            first = d;
        if (!last.isValid() || d > last)
            last = d;
    }

    QSet<QString> ids;
    for (const QJsonValue &v : list)
        ids.insert(str(v.toObject(), "id"));

    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString id = str(row, "id");
        const QString title = str(row, "title").trimmed();
        if (id.isEmpty() || title.isEmpty()) {
            skip("tasks", QStringLiteral("row without id or title"));
            continue;
        }

        const auto status = taskStatusFromString(str(row, "status"));
        if (!status)
            warn(QStringLiteral("task %1: unknown status '%2', set to pending").arg(id, str(row, "status")));
        const auto priority = priorityFromString(str(row, "priority"));
        const auto energy = energyFromString(str(row, "energy_required"));

        const qint64 createdAt = utcMs(row, "created_at");

        QVariant recurrence;
        if (boolean(row, "is_recurring")) {
            const QJsonValue raw = jsonColumn(row, "recurrence_pattern");
            QJsonObject patternJson = raw.isObject()
                ? raw.toObject()
                : QJsonObject{{QStringLiteral("type"), QStringLiteral("daily")}, {QStringLiteral("interval"), 1}};
            if (!raw.isObject())
                warn(QStringLiteral("task %1: recurring without a pattern, assumed daily (the web app's default)").arg(id));
            QString why;
            auto pattern = RecurrencePattern::fromJson(patternJson, &why);
            if (pattern) {
                if (!pattern->startDate.isValid()) {
                    const QDate created = QDateTime::fromMSecsSinceEpoch(createdAt, kUtc).toTimeZone(m_zone).date();
                    const QDate first = firstOccurrence.value(id);
                    pattern->startDate = first.isValid() ? std::min(first, created) : created;
                }
                // The web app put every-N-weeks tasks with a mid-week anchor on
                // the wrong weeks. Keep the rhythm the user has been living
                // with: re-anchor on the latest generated occurrence.
                const QDate last = lastOccurrence.value(id);
                if (pattern->type == RecurrencePattern::Type::Weekly && pattern->interval > 1 && last.isValid()
                    && last >= pattern->startDate && !pattern->occursOn(last)) {
                    warn(QStringLiteral("task %1: every-%2-weeks rhythm re-anchored on %3 to match existing occurrences")
                             .arg(id)
                             .arg(pattern->interval)
                             .arg(last.toString(Qt::ISODate)));
                    pattern->startDate = last;
                }
                recurrence = jsonText(pattern->toJson());
            } else {
                warn(QStringLiteral("task %1: unusable recurrence (%2), imported as one-off").arg(id, why));
            }
        }

        QString parent = str(row, "parent_task_id");
        if (!parent.isEmpty() && (!ids.contains(parent) || parent == id)) {
            warn(QStringLiteral("task %1: parent %2 not in export, detached").arg(id, parent));
            parent.clear();
        }

        QString category = str(row, "category_id");
        if (category.isEmpty() || !m_categories.contains(category))
            category = m_defaultCategory;

        const QDate due = parseLocalDate(str(row, "due_date"), m_zone);
        const auto complexity = number(row, "complexity");
        const auto estimate = number(row, "time_estimate_minutes");

        if (!insert("tasks", {
                {QStringLiteral("id"), id},
                {QStringLiteral("title"), title},
                {QStringLiteral("description"), optStr(row, "description")},
                {QStringLiteral("status"), nameOf(status.value_or(TaskStatus::Pending))},
                {QStringLiteral("priority"), nameOf(priority.value_or(Priority::Medium))},
                {QStringLiteral("due_date"), due.isValid() ? QVariant(due.toString(Qt::ISODate)) : QVariant()},
                {QStringLiteral("time_estimate_minutes"), estimate && *estimate > 0 ? qint64(*estimate) : 30},
                {QStringLiteral("complexity"), complexity ? std::clamp(qint64(*complexity), qint64(1), qint64(5)) : 3},
                {QStringLiteral("energy_required"), nameOf(energy.value_or(Energy::Medium))},
                {QStringLiteral("recurrence"), recurrence},
                {QStringLiteral("scheduled_time"), hhmm(str(row, "scheduled_time"))},
                {QStringLiteral("is_fixed_time"), boolean(row, "is_fixed_time") ? 1 : 0},
                {QStringLiteral("parent_task_id"), parent.isEmpty() ? QVariant() : QVariant(parent)},
                {QStringLiteral("category_id"), category},
                {QStringLiteral("tags"), tagsText(row, "tags")},
                {QStringLiteral("color"), optStr(row, "color")},
                {QStringLiteral("sort_order"), optInt(row, "sort_order")},
                {QStringLiteral("created_at"), createdAt},
                {QStringLiteral("updated_at"), utcMs(row, "updated_at")},
            }))
            return false;
        m_tasks.insert(id, status.value_or(TaskStatus::Pending));
    }
    return true;
}

bool Run::dependencies(const QJsonArray &list)
{
    DependencyGraph graph;
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString task = str(row, "task_id");
        const QString prereq = str(row, "depends_on_task_id");
        if (!m_tasks.contains(task) || !m_tasks.contains(prereq)) {
            skip("task_dependencies", QStringLiteral("%1 -> %2 references a missing task").arg(task, prereq));
            continue;
        }
        switch (graph.add(task, prereq)) {
        case DependencyGraph::AddResult::Added:
            break;
        case DependencyGraph::AddResult::AlreadyExists:
            continue;
        case DependencyGraph::AddResult::SelfDependency:
        case DependencyGraph::AddResult::WouldCreateCycle:
            skip("task_dependencies", QStringLiteral("%1 -> %2 would form a cycle").arg(task, prereq));
            continue;
        }
        if (!insert("task_dependencies", {
                {QStringLiteral("task_id"), task},
                {QStringLiteral("depends_on_task_id"), prereq},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
    }
    return true;
}

bool Run::occurrences(const QJsonArray &list)
{
    // The web app had no unique constraint on (task, date), so duplicates
    // exist. Keep one per day, preferring a row that records an outcome.
    QHash<QString, QJsonObject> chosen;
    QStringList order;
    int duplicates = 0;
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString task = str(row, "task_id");
        const QDate date = leadingDate(str(row, "occurrence_date"));
        if (!m_tasks.contains(task) || !date.isValid()) {
            skip("task_occurrences", QStringLiteral("%1 has a missing task or bad date").arg(str(row, "id")));
            continue;
        }
        const QString key = task + QLatin1Char('|') + date.toString(Qt::ISODate);
        auto it = chosen.find(key);
        if (it == chosen.end()) {
            chosen.insert(key, row);
            order.append(key);
            continue;
        }
        ++duplicates;
        ++m_report.skipped[QStringLiteral("task_occurrences")];
        if (str(*it, "status") == QLatin1String("pending") && str(row, "status") != QLatin1String("pending"))
            *it = row;
    }
    if (duplicates > 0)
        warn(QStringLiteral("task_occurrences: merged %1 duplicate rows for the same task and day").arg(duplicates));

    for (const QString &key : order) {
        const QJsonObject row = chosen.value(key);
        const auto status = occurrenceStatusFromString(str(row, "status")).value_or(OccurrenceStatus::Pending);
        if (!insert("task_occurrences", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("task_id"), str(row, "task_id")},
                {QStringLiteral("occurrence_date"), key.section(QLatin1Char('|'), 1)},
                {QStringLiteral("status"), nameOf(status)},
                {QStringLiteral("completed_at"), optUtcMs(row, "completed_at")},
                {QStringLiteral("scheduled_time"), hhmm(str(row, "scheduled_time"))},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
    }
    return true;
}

bool Run::workLogs(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString id = str(row, "id");
        if (!m_tasks.contains(str(row, "task_id"))) {
            skip("work_logs", QStringLiteral("%1 references a missing task").arg(id));
            continue;
        }
        if (!insert("work_logs", {
                {QStringLiteral("id"), id},
                {QStringLiteral("task_id"), str(row, "task_id")},
                {QStringLiteral("started_at"), utcMs(row, "started_at")},
                {QStringLiteral("ended_at"), optUtcMs(row, "ended_at")},
                {QStringLiteral("duration_minutes"), optInt(row, "duration_minutes")},
                {QStringLiteral("notes"), optStr(row, "notes")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
        m_workLogs.insert(id);
    }
    return true;
}

bool Run::schedules(const QJsonArray &list)
{
    int naive = 0;
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString id = str(row, "id");
        if (!m_tasks.contains(str(row, "task_id"))) {
            skip("schedules", QStringLiteral("%1 references a missing task").arg(id));
            continue;
        }
        const auto start = parseTimestamp(str(row, "scheduled_start"), m_zone);
        const auto end = parseTimestamp(str(row, "scheduled_end"), m_zone);
        if (!start || !end) {
            skip("schedules", QStringLiteral("%1 has unreadable times").arg(id));
            continue;
        }
        if (!start->hadOffset)
            ++naive;
        qint64 startMs = start->utc.toMSecsSinceEpoch();
        qint64 endMs = end->utc.toMSecsSinceEpoch();
        if (endMs < startMs) {
            warn(QStringLiteral("schedules %1: ends before it starts, end set to start").arg(id));
            endMs = startMs;
        }
        const QString workLog = str(row, "work_log_id");
        if (!insert("schedules", {
                {QStringLiteral("id"), id},
                {QStringLiteral("task_id"), str(row, "task_id")},
                {QStringLiteral("scheduled_start"), startMs},
                {QStringLiteral("scheduled_end"), endMs},
                {QStringLiteral("actual_start"), optLocalMs(row, "actual_start")},
                {QStringLiteral("actual_end"), optLocalMs(row, "actual_end")},
                {QStringLiteral("completed"), boolean(row, "completed") ? 1 : 0},
                {QStringLiteral("bumped_count"), optInt(row, "bumped_count").toLongLong()},
                {QStringLiteral("notes"), optStr(row, "notes")},
                {QStringLiteral("work_log_id"), m_workLogs.contains(workLog) ? QVariant(workLog) : QVariant()},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
    }
    if (naive > 0)
        warn(QStringLiteral("schedules: %1 rows had no timezone and were read as %2 local time")
                 .arg(naive)
                 .arg(QString::fromUtf8(m_zone.id())));
    return true;
}

bool Run::events(const QJsonArray &list)
{
    QMap<QString, QStringList> otherZones;
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString id = str(row, "id");
        const QDate startDate = leadingDate(str(row, "start_date"));
        const QDate endDate = leadingDate(str(row, "end_date"));
        if (!startDate.isValid()) {
            skip("events", QStringLiteral("%1 has no start date").arg(id));
            continue;
        }

        bool allDay = boolean(row, "is_all_day");
        QVariant startAt, endAt, startDay, endDay;
        if (!allDay) {
            // Timed events hold wall-clock times in the zone named by their own
            // timezone column; the web app fell back to Sydney for rows without one.
            QString tzName = str(row, "timezone");
            if (tzName.isEmpty())
                tzName = QStringLiteral("Australia/Sydney");
            QTimeZone eventZone(tzName.toUtf8());
            if (!eventZone.isValid()) {
                warn(QStringLiteral("events %1: unknown timezone '%2', read as %3").arg(id, tzName, QString::fromUtf8(m_zone.id())));
                eventZone = m_zone;
            }
            const QTime st = QTime::fromString(hhmm(str(row, "start_time")).toString(), QStringLiteral("HH:mm"));
            const QTime et = QTime::fromString(hhmm(str(row, "end_time")).toString(), QStringLiteral("HH:mm"));
            if (st.isValid() && et.isValid()) {
                const QDateTime s(startDate, st, eventZone);
                QDateTime e(endDate.isValid() ? endDate : startDate, et, eventZone);
                if (e < s)
                    e = s;
                startAt = s.toMSecsSinceEpoch();
                endAt = e.toMSecsSinceEpoch();
                if (eventZone != m_zone)
                    otherZones[QString::fromUtf8(eventZone.id())].append(str(row, "title"));
            } else {
                warn(QStringLiteral("events %1: timed event without times, imported as all-day").arg(id));
                allDay = true;
            }
        }
        if (allDay) {
            startDay = startDate.toString(Qt::ISODate);
            endDay = (endDate.isValid() && endDate >= startDate ? endDate : startDate).toString(Qt::ISODate);
        }

        const auto mode = blockingModeFromString(str(row, "blocking_mode")).value_or(BlockingMode::None);
        const auto real = [&row](const char *key) {
            const auto n = number(row, key);
            return n ? QVariant(*n) : QVariant();
        };

        if (!insert("events", {
                {QStringLiteral("id"), id},
                {QStringLiteral("title"), str(row, "title").isEmpty() ? QStringLiteral("(untitled)") : str(row, "title")},
                {QStringLiteral("description"), optStr(row, "description")},
                {QStringLiteral("is_all_day"), allDay ? 1 : 0},
                {QStringLiteral("start_date"), startDay},
                {QStringLiteral("end_date"), endDay},
                {QStringLiteral("start_at"), startAt},
                {QStringLiteral("end_at"), endAt},
                {QStringLiteral("timezone"), optStr(row, "timezone")},
                {QStringLiteral("blocking_mode"), nameOf(mode)},
                {QStringLiteral("allowed_tags"), tagsText(row, "allowed_tags")},
                {QStringLiteral("color"), str(row, "color").isEmpty() ? QStringLiteral("#6366f1") : str(row, "color")},
                {QStringLiteral("location"), optStr(row, "location")},
                {QStringLiteral("latitude"), real("latitude")},
                {QStringLiteral("longitude"), real("longitude")},
                {QStringLiteral("is_travel_event"), boolean(row, "is_travel_event") ? 1 : 0},
                {QStringLiteral("start_location"), optStr(row, "start_location")},
                {QStringLiteral("end_location"), optStr(row, "end_location")},
                {QStringLiteral("start_latitude"), real("start_latitude")},
                {QStringLiteral("start_longitude"), real("start_longitude")},
                {QStringLiteral("end_latitude"), real("end_latitude")},
                {QStringLiteral("end_longitude"), real("end_longitude")},
                {QStringLiteral("transport_type"), optStr(row, "transport_type")},
                {QStringLiteral("flight_number"), optStr(row, "flight_number")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
                {QStringLiteral("updated_at"), utcMs(row, "updated_at")},
            }))
            return false;
        m_events.insert(id);
    }
    // Times typed while the browser sat in another zone (travelling, say) are
    // kept as entered, which is how the web app showed them too. Flag them.
    for (auto it = otherZones.cbegin(); it != otherZones.cend(); ++it)
        warn(QStringLiteral("events: %1 entered in %2, not %3; check their times: %4")
                 .arg(it.value().size())
                 .arg(it.key(), QString::fromUtf8(m_zone.id()), it.value().join(QStringLiteral(", "))));
    return true;
}

bool Run::energyLogs(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const auto level = number(row, "energy_level");
        const auto at = parseTimestampMs(str(row, "timestamp"), kUtc);
        if (!level || *level < 1 || *level > 10 || !at) {
            skip("energy_logs", QStringLiteral("%1 has no usable level or time").arg(str(row, "id")));
            continue;
        }
        if (!insert("energy_logs", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("logged_at"), *at},
                {QStringLiteral("energy_level"), qint64(*level)},
                {QStringLiteral("notes"), optStr(row, "notes")},
            }))
            return false;
    }
    return true;
}

bool Run::healthLogs(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QDate date = parseLocalDate(str(row, "log_date"), m_zone);
        if (!date.isValid()) {
            skip("health_logs", QStringLiteral("%1 has no date").arg(str(row, "id")));
            continue;
        }
        const auto hours = number(row, "hours_slept");
        if (!insert("health_logs", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("log_date"), date.toString(Qt::ISODate)},
                {QStringLiteral("sleep_score"), optInt(row, "sleep_score")},
                {QStringLiteral("readiness_score"), optInt(row, "readiness_score")},
                {QStringLiteral("hours_slept"), hours ? QVariant(*hours) : QVariant()},
                {QStringLiteral("notes"), optStr(row, "notes")},
                {QStringLiteral("source"), str(row, "source").isEmpty() ? QStringLiteral("manual") : str(row, "source")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
                {QStringLiteral("updated_at"), utcMs(row, "updated_at")},
            }))
            return false;
    }
    return true;
}

bool Run::healthSummaries(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QDate date = parseLocalDate(str(row, "summary_date"), m_zone);
        if (!date.isValid() || str(row, "summary_text").isEmpty()) {
            skip("health_summaries", QStringLiteral("%1 has no date or text").arg(str(row, "id")));
            continue;
        }
        if (!insert("health_summaries", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("summary_date"), date.toString(Qt::ISODate)},
                {QStringLiteral("summary_text"), str(row, "summary_text")},
                {QStringLiteral("data_context"), optJsonText(row, "data_context")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
    }
    return true;
}

bool Run::taskHistory(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString task = str(row, "task_id");
        if (!insert("task_history", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("task_id"), m_tasks.contains(task) ? QVariant(task) : QVariant()},
                {QStringLiteral("action"), str(row, "action")},
                {QStringLiteral("at"), utcMs(row, "timestamp")},
                {QStringLiteral("details"), optJsonText(row, "details")},
            }))
            return false;
    }
    return true;
}

bool Run::aiLogs(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        if (!insert("ai_logs", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("action_type"), str(row, "action_type")},
                {QStringLiteral("request_context"), optJsonText(row, "request_context")},
                {QStringLiteral("response"), optJsonText(row, "response")},
                {QStringLiteral("user_feedback"), optStr(row, "user_feedback")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
    }
    return true;
}

bool Run::notes(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString task = str(row, "task_id");
        if (!insert("notes", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("title"), str(row, "title").isEmpty() ? QStringLiteral("(untitled)") : str(row, "title")},
                {QStringLiteral("content"), str(row, "content")},
                {QStringLiteral("task_id"), m_tasks.contains(task) ? QVariant(task) : QVariant()},
                {QStringLiteral("tags"), tagsText(row, "tags")},
                {QStringLiteral("color"), optStr(row, "color")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
                {QStringLiteral("updated_at"), utcMs(row, "updated_at")},
            }))
            return false;
    }
    return true;
}

bool Run::integrations(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        const QString id = str(row, "id");
        const QJsonValue config = jsonColumn(row, "config");
        if (!insert("integrations", {
                {QStringLiteral("id"), id},
                {QStringLiteral("provider"), str(row, "provider")},
                {QStringLiteral("config"), jsonText(config.isObject() ? config : QJsonObject())},
                {QStringLiteral("last_sync_at"), optUtcMs(row, "last_sync_at")},
                // Tokens are never exported, so every integration must be
                // reconnected before it can sync.
                {QStringLiteral("sync_status"), QStringLiteral("needs_auth")},
                {QStringLiteral("sync_error"), QStringLiteral("Reconnect after import")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
                {QStringLiteral("updated_at"), utcMs(row, "updated_at")},
            }))
            return false;
        m_integrations.insert(id);
    }
    if (!list.isEmpty())
        warn(QStringLiteral("integrations: %1 imported without credentials; reconnect them in Settings").arg(list.size()));
    return true;
}

bool Run::syncedItems(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        if (!m_integrations.contains(str(row, "integration_id")) || !m_tasks.contains(str(row, "task_id"))) {
            skip("synced_items", QStringLiteral("%1 references a missing task or integration").arg(str(row, "id")));
            continue;
        }
        if (!insert("synced_items", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("integration_id"), str(row, "integration_id")},
                {QStringLiteral("external_id"), str(row, "external_id")},
                {QStringLiteral("external_updated_at"), optUtcMs(row, "external_updated_at")},
                {QStringLiteral("task_id"), str(row, "task_id")},
                {QStringLiteral("data_hash"), optStr(row, "data_hash")},
                {QStringLiteral("last_synced_at"), utcMs(row, "last_synced_at")},
                {QStringLiteral("sync_status"), str(row, "sync_status").isEmpty() ? QStringLiteral("synced") : str(row, "sync_status")},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
    }
    return true;
}

bool Run::syncedEvents(const QJsonArray &list)
{
    for (const QJsonValue &v : list) {
        const QJsonObject row = v.toObject();
        if (!m_integrations.contains(str(row, "integration_id")) || !m_events.contains(str(row, "event_id"))) {
            skip("synced_events", QStringLiteral("%1 references a missing event or integration").arg(str(row, "id")));
            continue;
        }
        const QString status = str(row, "sync_status") == QLatin1String("missing_external") ? QStringLiteral("missing_external")
                                                                                           : QStringLiteral("synced");
        if (!insert("synced_events", {
                {QStringLiteral("id"), str(row, "id")},
                {QStringLiteral("integration_id"), str(row, "integration_id")},
                {QStringLiteral("external_calendar_id"), str(row, "external_calendar_id")},
                {QStringLiteral("external_event_id"), str(row, "external_event_id")},
                {QStringLiteral("external_updated_at"), optUtcMs(row, "external_updated_at")},
                {QStringLiteral("event_id"), str(row, "event_id")},
                {QStringLiteral("data_hash"), optStr(row, "data_hash")},
                {QStringLiteral("last_synced_at"), utcMs(row, "last_synced_at")},
                {QStringLiteral("sync_status"), status},
                {QStringLiteral("created_at"), utcMs(row, "created_at")},
            }))
            return false;
    }
    return true;
}

} // namespace

Importer::Importer(Database &db)
    : m_db(db)
{
}

bool Importer::run(const QJsonObject &doc, ImportReport *report, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (doc.value(QLatin1String("format")).toString() != QLatin1String("task-tumbler-export"))
        return fail(QStringLiteral("not a Task Tumbler export"));
    const int version = doc.value(QLatin1String("version")).toInt();
    if (version != kSupportedVersion)
        return fail(QStringLiteral("unsupported export version %1 (expected %2)").arg(version).arg(kSupportedVersion));

    if (m_db.schemaVersion() != Database::latestSchemaVersion())
        return fail(QStringLiteral("database is not migrated"));

    QSqlDatabase db = m_db.handle();
    if (!m_allowNonEmpty) {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM tasks) + (SELECT COUNT(*) FROM categories)")) && q.next()
            && q.value(0).toInt() > 0)
            return fail(QStringLiteral("database already has data; import only into a new database"));
    }

    ImportReport local;
    ImportReport &r = report ? *report : local;
    r = ImportReport{};
    r.sourceKind = doc.value(QLatin1String("source")).toObject().value(QLatin1String("kind")).toString();
    r.userEmail = doc.value(QLatin1String("user")).toObject().value(QLatin1String("email")).toString();

    QTimeZone zone = m_fallbackZone;
    const QString tzName = doc.value(QLatin1String("preferences")).toObject().value(QLatin1String("timezone")).toString();
    if (!tzName.isEmpty()) {
        const QTimeZone tz(tzName.toUtf8());
        if (tz.isValid())
            zone = tz;
        else
            r.warnings.append(QStringLiteral("unknown timezone '%1', using %2").arg(tzName, QString::fromUtf8(zone.id())));
    }
    r.timezone = QString::fromUtf8(zone.id());

    if (!db.transaction())
        return fail(db.lastError().text());

    // Rows reference each other in every direction (parent tasks, schedules to
    // work logs); check foreign keys once, at commit.
    QSqlQuery(db).exec(QStringLiteral("PRAGMA defer_foreign_keys = ON"));

    Run importRun(db, r, zone, QDateTime::currentMSecsSinceEpoch());
    QString runError;
    if (!importRun.importAll(doc, &runError)) {
        db.rollback();
        return fail(runError);
    }

    QSqlQuery check(db);
    if (check.exec(QStringLiteral("PRAGMA foreign_key_check")) && check.next()) {
        const QString detail = QStringLiteral("%1 row %2 -> %3").arg(check.value(0).toString(), check.value(1).toString(),
                                                                     check.value(2).toString());
        db.rollback();
        return fail(QStringLiteral("foreign key violation after import: %1").arg(detail));
    }

    if (!db.commit()) {
        const QString message = db.lastError().text();
        db.rollback();
        return fail(message);
    }
    return true;
}

} // namespace tumbler
