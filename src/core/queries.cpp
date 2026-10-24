#include "queries.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>

namespace tumbler {

namespace {

QJsonValue parseJson(const QVariant &v)
{
    if (v.isNull())
        return QJsonValue::Null;
    const QByteArray text = v.toString().toUtf8();
    // Scalars need wrapping for QJsonDocument to parse them.
    const QJsonDocument doc = QJsonDocument::fromJson("[" + text + "]");
    return doc.isArray() ? doc.array().first() : QJsonValue();
}

QStringList stringList(const QVariant &v)
{
    QStringList out;
    for (const QJsonValue &item : parseJson(v).toArray())
        out.append(item.toString());
    return out;
}

std::optional<qint64> optMs(const QVariant &v)
{
    return v.isNull() ? std::nullopt : std::optional<qint64>(v.toLongLong());
}

QTime hhmm(const QVariant &v)
{
    return v.isNull() ? QTime() : QTime::fromString(v.toString(), QStringLiteral("HH:mm"));
}

Task readTask(const QSqlQuery &q)
{
    const QSqlRecord r = q.record();
    const auto col = [&](const char *name) { return q.value(r.indexOf(QLatin1String(name))); };

    Task t;
    t.id = col("id").toString();
    t.title = col("title").toString();
    t.description = col("description").toString();
    t.status = taskStatusFromString(col("status").toString()).value_or(TaskStatus::Pending);
    t.priority = priorityFromString(col("priority").toString()).value_or(Priority::Medium);
    t.dueDate = QDate::fromString(col("due_date").toString(), Qt::ISODate);
    t.estimateMinutes = col("time_estimate_minutes").toInt();
    t.complexity = col("complexity").toInt();
    t.energy = energyFromString(col("energy_required").toString()).value_or(Energy::Medium);
    if (!col("recurrence").isNull())
        t.recurrence = RecurrencePattern::fromJson(parseJson(col("recurrence")).toObject());
    t.scheduledTime = hhmm(col("scheduled_time"));
    t.fixedTime = col("is_fixed_time").toBool();
    t.parentId = col("parent_task_id").toString();
    t.categoryId = col("category_id").toString();
    t.tags = stringList(col("tags"));
    t.color = col("color").toString();
    if (!col("sort_order").isNull())
        t.sortOrder = col("sort_order").toInt();
    t.createdAt = col("created_at").toLongLong();
    t.updatedAt = col("updated_at").toLongLong();
    return t;
}

ScheduleBlock readSchedule(const QSqlQuery &q)
{
    ScheduleBlock s;
    s.id = q.value(0).toString();
    s.taskId = q.value(1).toString();
    s.start = q.value(2).toLongLong();
    s.end = q.value(3).toLongLong();
    s.actualStart = optMs(q.value(4));
    s.actualEnd = optMs(q.value(5));
    s.completed = q.value(6).toBool();
    s.bumpedCount = q.value(7).toInt();
    s.notes = q.value(8).toString();
    s.workLogId = q.value(9).toString();
    return s;
}

const char *const kScheduleColumns =
    "id, task_id, scheduled_start, scheduled_end, actual_start, actual_end, completed, bumped_count, notes, work_log_id";

Occurrence readOccurrence(const QSqlQuery &q)
{
    Occurrence o;
    o.id = q.value(0).toString();
    o.taskId = q.value(1).toString();
    o.date = QDate::fromString(q.value(2).toString(), Qt::ISODate);
    o.status = occurrenceStatusFromString(q.value(3).toString()).value_or(OccurrenceStatus::Pending);
    o.completedAt = optMs(q.value(4));
    o.scheduledTime = hhmm(q.value(5));
    return o;
}

const char *const kOccurrenceColumns = "id, task_id, occurrence_date, status, completed_at, scheduled_time";

WorkLog readWorkLog(const QSqlQuery &q)
{
    WorkLog w;
    w.id = q.value(0).toString();
    w.taskId = q.value(1).toString();
    w.startedAt = q.value(2).toLongLong();
    w.endedAt = optMs(q.value(3));
    if (!q.value(4).isNull())
        w.durationMinutes = q.value(4).toInt();
    w.notes = q.value(5).toString();
    return w;
}

const char *const kWorkLogColumns = "id, task_id, started_at, ended_at, duration_minutes, notes";

QSqlQuery run(QSqlDatabase db, const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery q(db);
    q.setForwardOnly(true);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    q.exec();
    return q;
}

} // namespace

Queries::Queries(QSqlDatabase db)
    : m_db(db)
{
}

QList<Task> Queries::tasks() const
{
    QList<Task> out;
    QSqlQuery q = run(m_db, QStringLiteral("SELECT * FROM tasks ORDER BY COALESCE(sort_order, 1 << 30), created_at"));
    while (q.next())
        out.append(readTask(q));
    return out;
}

std::optional<Task> Queries::task(const QString &id) const
{
    QSqlQuery q = run(m_db, QStringLiteral("SELECT * FROM tasks WHERE id = ?"), {id});
    if (!q.next())
        return std::nullopt;
    return readTask(q);
}

QList<Category> Queries::categories() const
{
    QList<Category> out;
    QSqlQuery q = run(m_db, QStringLiteral(
        "SELECT id, name, schedule, color, is_default, show_on_schedule FROM categories ORDER BY is_default DESC, name"));
    while (q.next()) {
        Category c;
        c.id = q.value(0).toString();
        c.name = q.value(1).toString();
        c.schedule = CategorySchedule::fromJson(parseJson(q.value(2)).toObject());
        c.color = q.value(3).toString();
        c.isDefault = q.value(4).toBool();
        c.showOnSchedule = q.value(5).toBool();
        out.append(c);
    }
    return out;
}

std::optional<Category> Queries::defaultCategory() const
{
    for (const Category &c : categories()) {
        if (c.isDefault)
            return c;
    }
    return std::nullopt;
}

QList<Dependency> Queries::dependencies() const
{
    QList<Dependency> out;
    QSqlQuery q = run(m_db, QStringLiteral("SELECT task_id, depends_on_task_id FROM task_dependencies"));
    while (q.next())
        out.append({q.value(0).toString(), q.value(1).toString()});
    return out;
}

QList<ScheduleBlock> Queries::schedules(qint64 from, qint64 to) const
{
    QList<ScheduleBlock> out;
    QSqlQuery q = run(m_db,
                      QStringLiteral("SELECT %1 FROM schedules WHERE scheduled_start < ? AND scheduled_end > ? "
                                     "ORDER BY scheduled_start")
                          .arg(QLatin1String(kScheduleColumns)),
                      {to, from});
    while (q.next())
        out.append(readSchedule(q));
    return out;
}

QList<ScheduleBlock> Queries::schedulesForTask(const QString &taskId) const
{
    QList<ScheduleBlock> out;
    QSqlQuery q = run(m_db,
                      QStringLiteral("SELECT %1 FROM schedules WHERE task_id = ? ORDER BY scheduled_start")
                          .arg(QLatin1String(kScheduleColumns)),
                      {taskId});
    while (q.next())
        out.append(readSchedule(q));
    return out;
}

std::optional<ScheduleBlock> Queries::schedule(const QString &id) const
{
    QSqlQuery q = run(m_db, QStringLiteral("SELECT %1 FROM schedules WHERE id = ?").arg(QLatin1String(kScheduleColumns)), {id});
    if (!q.next())
        return std::nullopt;
    return readSchedule(q);
}

QList<Occurrence> Queries::occurrences(QDate from, QDate to) const
{
    QList<Occurrence> out;
    QSqlQuery q = run(m_db,
                      QStringLiteral("SELECT %1 FROM task_occurrences WHERE occurrence_date BETWEEN ? AND ? "
                                     "ORDER BY occurrence_date")
                          .arg(QLatin1String(kOccurrenceColumns)),
                      {from.toString(Qt::ISODate), to.toString(Qt::ISODate)});
    while (q.next())
        out.append(readOccurrence(q));
    return out;
}

std::optional<Occurrence> Queries::occurrence(const QString &id) const
{
    QSqlQuery q = run(m_db, QStringLiteral("SELECT %1 FROM task_occurrences WHERE id = ?").arg(QLatin1String(kOccurrenceColumns)),
                      {id});
    if (!q.next())
        return std::nullopt;
    return readOccurrence(q);
}

QList<Event> Queries::events(qint64 from, qint64 to, QDate firstDay, QDate lastDay) const
{
    QList<Event> out;
    QSqlQuery q = run(m_db,
                      QStringLiteral("SELECT id, title, is_all_day, start_date, end_date, start_at, end_at, blocking_mode, "
                                     "allowed_tags, color, location FROM events "
                                     "WHERE (is_all_day = 0 AND start_at < ? AND end_at > ?) "
                                     "   OR (is_all_day = 1 AND start_date <= ? AND end_date >= ?) "
                                     "ORDER BY is_all_day DESC, start_at, start_date"),
                      {to, from, lastDay.toString(Qt::ISODate), firstDay.toString(Qt::ISODate)});
    while (q.next()) {
        Event e;
        e.id = q.value(0).toString();
        e.title = q.value(1).toString();
        e.allDay = q.value(2).toBool();
        e.startDate = QDate::fromString(q.value(3).toString(), Qt::ISODate);
        e.endDate = QDate::fromString(q.value(4).toString(), Qt::ISODate);
        e.startAt = q.value(5).toLongLong();
        e.endAt = q.value(6).toLongLong();
        e.blocking.mode = blockingModeFromString(q.value(7).toString()).value_or(BlockingMode::None);
        e.blocking.allowedTags = stringList(q.value(8));
        e.color = q.value(9).toString();
        e.location = q.value(10).toString();
        out.append(e);
    }
    return out;
}

std::optional<WorkLog> Queries::activeWorkLog() const
{
    QSqlQuery q = run(m_db,
                      QStringLiteral("SELECT %1 FROM work_logs WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1")
                          .arg(QLatin1String(kWorkLogColumns)));
    if (!q.next())
        return std::nullopt;
    return readWorkLog(q);
}

QList<WorkLog> Queries::workLogsForTask(const QString &taskId) const
{
    QList<WorkLog> out;
    QSqlQuery q = run(m_db,
                      QStringLiteral("SELECT %1 FROM work_logs WHERE task_id = ? ORDER BY started_at DESC")
                          .arg(QLatin1String(kWorkLogColumns)),
                      {taskId});
    while (q.next())
        out.append(readWorkLog(q));
    return out;
}

QHash<QString, int> Queries::loggedMinutes() const
{
    QHash<QString, int> out;
    QSqlQuery q = run(m_db, QStringLiteral("SELECT task_id, SUM(duration_minutes) FROM work_logs "
                                           "WHERE duration_minutes IS NOT NULL GROUP BY task_id"));
    while (q.next())
        out.insert(q.value(0).toString(), q.value(1).toInt());
    return out;
}

QJsonValue Queries::setting(const QString &key, const QJsonValue &fallback) const
{
    QSqlQuery q = run(m_db, QStringLiteral("SELECT value FROM settings WHERE key = ?"), {key});
    if (!q.next())
        return fallback;
    const QJsonValue v = parseJson(q.value(0));
    return v.isUndefined() ? fallback : v;
}

bool Queries::isEmpty() const
{
    QSqlQuery q = run(m_db, QStringLiteral("SELECT (SELECT COUNT(*) FROM tasks) + (SELECT COUNT(*) FROM categories)"));
    return q.next() && q.value(0).toInt() == 0;
}

} // namespace tumbler
