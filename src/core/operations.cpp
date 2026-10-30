#include "operations.h"

#include "dependencygraph.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace tumbler {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

namespace {

constexpr qint64 kMinute = 60 * 1000;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool exec(QSqlDatabase db, const QString &sql, const QVariantList &binds, QString *error)
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec()) {
        setError(error, QStringLiteral("%1 [%2]").arg(q.lastError().text(), sql.left(80)));
        return false;
    }
    return true;
}

QString jsonText(const QJsonValue &v)
{
    if (v.isObject())
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    const QByteArray wrapped = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}

QString tagsJson(const QStringList &tags)
{
    QJsonArray arr;
    for (const QString &t : tags) {
        const QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !arr.contains(trimmed))
            arr.append(trimmed);
    }
    return jsonText(arr);
}

QVariant optString(const QString &s)
{
    return s.isEmpty() ? QVariant() : QVariant(s);
}

QVariant timeText(const QTime &t)
{
    return t.isValid() ? QVariant(t.toString(QStringLiteral("HH:mm"))) : QVariant();
}

QString historyAction(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Completed:
        return QStringLiteral("completed");
    case TaskStatus::InProgress:
        return QStringLiteral("started");
    case TaskStatus::Cancelled:
        return QStringLiteral("cancelled");
    case TaskStatus::Pending:
        return QStringLiteral("reopened");
    }
    return {};
}

bool addHistory(QSqlDatabase db, const QString &taskId, const QString &action, qint64 now, const QJsonObject &details,
                QString *error)
{
    return exec(db, QStringLiteral("INSERT INTO task_history (id, task_id, action, at, details) VALUES (?, ?, ?, ?, ?)"),
                {newId(), taskId, action, now, details.isEmpty() ? QVariant() : QVariant(jsonText(details))}, error);
}

// Ends every open work session at `now`.
bool endActiveWork(QSqlDatabase db, qint64 now, QString *error, const QString &onlyTask = {})
{
    QString sql = QStringLiteral(
        "UPDATE work_logs SET ended_at = ?, duration_minutes = MAX(0, CAST(ROUND((? - started_at) / 60000.0) AS INTEGER)) "
        "WHERE ended_at IS NULL");
    QVariantList binds{now, now};
    if (!onlyTask.isEmpty()) {
        sql += QStringLiteral(" AND task_id = ?");
        binds.append(onlyTask);
    }
    return exec(db, sql, binds, error);
}

} // namespace

namespace ops {

Mutation createTask(const Task &task, qint64 now)
{
    Mutation m;
    m.text = QStringLiteral("Add “%1”").arg(task.title);
    m.scopes = {{QStringLiteral("tasks"), QStringLiteral("id = ?"), {task.id}}};
    m.apply = [task, now](QSqlDatabase db, QString *error) {
        QVariant recurrence;
        if (task.recurrence) {
            RecurrencePattern p = *task.recurrence;
            if (!p.startDate.isValid())
                p.startDate = QDateTime::fromMSecsSinceEpoch(now).date();
            recurrence = jsonText(p.toJson());
        }
        return exec(db,
                    QStringLiteral("INSERT INTO tasks (id, title, description, status, priority, due_date, "
                                   "time_estimate_minutes, complexity, energy_required, recurrence, scheduled_time, "
                                   "is_fixed_time, parent_task_id, category_id, tags, color, sort_order, created_at, updated_at) "
                                   "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                    {task.id, task.title.trimmed(), optString(task.description), nameOf(task.status),
                     nameOf(task.priority), task.dueDate.isValid() ? QVariant(task.dueDate.toString(Qt::ISODate)) : QVariant(),
                     std::max(1, task.estimateMinutes), std::clamp(task.complexity, 1, 5), nameOf(task.energy), recurrence,
                     timeText(task.scheduledTime), task.fixedTime ? 1 : 0, optString(task.parentId),
                     optString(task.categoryId), tagsJson(task.tags), optString(task.color),
                     task.sortOrder ? QVariant(*task.sortOrder) : QVariant(), now, now},
                    error);
    };
    return m;
}

std::optional<Mutation> updateTask(const Queries &q, const QString &id, const QVariantMap &fields, qint64 now,
                                   QString *error)
{
    const auto task = q.task(id);
    if (!task) {
        setError(error, QStringLiteral("task not found"));
        return std::nullopt;
    }

    QStringList sets;
    QVariantList binds;
    const auto set = [&](const char *column, const QVariant &value) {
        sets.append(QLatin1String(column) + QStringLiteral(" = ?"));
        binds.append(value);
    };

    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString &key = it.key();
        const QVariant &v = it.value();
        if (key == QLatin1String("title")) {
            const QString title = v.toString().trimmed();
            if (title.isEmpty()) {
                setError(error, QStringLiteral("a task needs a title"));
                return std::nullopt;
            }
            set("title", title);
        } else if (key == QLatin1String("description") || key == QLatin1String("color")) {
            set(key == QLatin1String("color") ? "color" : "description", optString(v.toString()));
        } else if (key == QLatin1String("priority")) {
            const auto p = priorityFromString(v.toString());
            if (!p) {
                setError(error, QStringLiteral("unknown priority"));
                return std::nullopt;
            }
            set("priority", nameOf(*p));
        } else if (key == QLatin1String("energy_required")) {
            const auto e = energyFromString(v.toString());
            if (!e) {
                setError(error, QStringLiteral("unknown energy level"));
                return std::nullopt;
            }
            set("energy_required", nameOf(*e));
        } else if (key == QLatin1String("due_date")) {
            const QDate d = v.typeId() == QMetaType::QDate ? v.toDate() : QDate::fromString(v.toString(), Qt::ISODate);
            set("due_date", d.isValid() ? QVariant(d.toString(Qt::ISODate)) : QVariant());
        } else if (key == QLatin1String("time_estimate_minutes")) {
            set("time_estimate_minutes", std::max(1, v.toInt()));
        } else if (key == QLatin1String("complexity")) {
            set("complexity", std::clamp(v.toInt(), 1, 5));
        } else if (key == QLatin1String("scheduled_time")) {
            set("scheduled_time", timeText(QTime::fromString(v.toString(), QStringLiteral("HH:mm"))));
        } else if (key == QLatin1String("is_fixed_time")) {
            set("is_fixed_time", v.toBool() ? 1 : 0);
        } else if (key == QLatin1String("parent_task_id")) {
            const QString parent = v.toString();
            // Walk up from the new parent; meeting this task means a loop.
            for (QString p = parent; !p.isEmpty();) {
                if (p == id) {
                    setError(error, QStringLiteral("a task cannot be its own ancestor"));
                    return std::nullopt;
                }
                const auto pt = q.task(p);
                p = pt ? pt->parentId : QString();
            }
            set("parent_task_id", optString(parent));
        } else if (key == QLatin1String("category_id")) {
            set("category_id", optString(v.toString()));
        } else if (key == QLatin1String("tags")) {
            set("tags", tagsJson(v.toStringList()));
        } else if (key == QLatin1String("recurrence")) {
            if (v.isNull() || !v.isValid() || v.toMap().isEmpty()) {
                set("recurrence", QVariant());
            } else {
                QString why;
                auto p = RecurrencePattern::fromJson(QJsonObject::fromVariantMap(v.toMap()), &why);
                if (!p) {
                    setError(error, why);
                    return std::nullopt;
                }
                if (!p->startDate.isValid())
                    p->startDate = QDateTime::fromMSecsSinceEpoch(now).date();
                set("recurrence", jsonText(p->toJson()));
            }
        } else if (key == QLatin1String("sort_order")) {
            set("sort_order", v.isNull() ? QVariant() : QVariant(v.toInt()));
        } else {
            setError(error, QStringLiteral("field %1 cannot be edited").arg(key));
            return std::nullopt;
        }
    }

    set("updated_at", now);
    binds.append(id);

    Mutation m;
    m.text = QStringLiteral("Edit “%1”").arg(task->title);
    m.scopes = {{QStringLiteral("tasks"), QStringLiteral("id = ?"), {id}}};
    m.apply = [sets, binds](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("UPDATE tasks SET %1 WHERE id = ?").arg(sets.join(QStringLiteral(", "))), binds, error);
    };
    return m;
}

std::optional<Mutation> setTaskStatus(const Queries &q, const QString &id, TaskStatus status, qint64 now, QString *error)
{
    const auto task = q.task(id);
    if (!task) {
        setError(error, QStringLiteral("task not found"));
        return std::nullopt;
    }
    const QString parent = task->parentId;

    Mutation m;
    switch (status) {
    case TaskStatus::Completed:
        m.text = QStringLiteral("Complete “%1”").arg(task->title);
        break;
    case TaskStatus::InProgress:
        m.text = QStringLiteral("Start “%1”").arg(task->title);
        break;
    case TaskStatus::Cancelled:
        m.text = QStringLiteral("Cancel “%1”").arg(task->title);
        break;
    case TaskStatus::Pending:
        m.text = QStringLiteral("Reopen “%1”").arg(task->title);
        break;
    }
    m.scopes = {
        {QStringLiteral("tasks"), QStringLiteral("id IN (?, ?)"), {id, parent}},
        {QStringLiteral("task_history"), QStringLiteral("task_id IN (?, ?)"), {id, parent}},
        {QStringLiteral("work_logs"), QStringLiteral("ended_at IS NULL AND task_id = ?"), {id}},
    };
    m.apply = [id, parent, status, now](QSqlDatabase db, QString *error) {
        if (!exec(db, QStringLiteral("UPDATE tasks SET status = ?, updated_at = ? WHERE id = ?"), {nameOf(status), now, id}, error)
            || !addHistory(db, id, historyAction(status), now, {}, error))
            return false;

        if (isFinished(status) && !endActiveWork(db, now, error, id))
            return false;

        if (status == TaskStatus::Completed && !parent.isEmpty()) {
            QSqlQuery siblings(db);
            siblings.prepare(QStringLiteral("SELECT status FROM tasks WHERE parent_task_id = ?"));
            siblings.addBindValue(parent);
            if (!siblings.exec()) {
                setError(error, siblings.lastError().text());
                return false;
            }
            QList<TaskStatus> statuses;
            while (siblings.next())
                statuses.append(taskStatusFromString(siblings.value(0).toString()).value_or(TaskStatus::Pending));

            QSqlQuery parentStatus(db);
            parentStatus.prepare(QStringLiteral("SELECT status FROM tasks WHERE id = ?"));
            parentStatus.addBindValue(parent);
            const bool parentOpen = parentStatus.exec() && parentStatus.next()
                && !isFinished(taskStatusFromString(parentStatus.value(0).toString()).value_or(TaskStatus::Pending));

            if (parentOpen && shouldAutoCompleteParent(statuses)) {
                if (!exec(db, QStringLiteral("UPDATE tasks SET status = 'completed', updated_at = ? WHERE id = ?"), {now, parent}, error)
                    || !addHistory(db, parent, QStringLiteral("completed"), now,
                                   {{QStringLiteral("reason"), QStringLiteral("all_subtasks_completed")}}, error))
                    return false;
            }
        }
        return true;
    };
    return m;
}

Mutation deleteTask(const QString &id)
{
    Mutation m;
    m.text = QStringLiteral("Delete task");
    m.scopes = {
        {QStringLiteral("tasks"), QStringLiteral("id = ? OR parent_task_id = ?"), {id, id}},
        {QStringLiteral("task_dependencies"), QStringLiteral("task_id = ? OR depends_on_task_id = ?"), {id, id}},
        {QStringLiteral("task_occurrences"), QStringLiteral("task_id = ?"), {id}},
        {QStringLiteral("work_logs"), QStringLiteral("task_id = ?"), {id}},
        {QStringLiteral("schedules"), QStringLiteral("task_id = ? OR work_log_id IN (SELECT id FROM work_logs WHERE task_id = ?)"), {id, id}},
        {QStringLiteral("notes"), QStringLiteral("task_id = ?"), {id}},
        {QStringLiteral("task_history"), QStringLiteral("task_id = ?"), {id}},
        {QStringLiteral("synced_items"), QStringLiteral("task_id = ?"), {id}},
        {QStringLiteral("task_actions"), QStringLiteral("task_id = ?"), {id}},
    };
    m.apply = [id](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("DELETE FROM tasks WHERE id = ?"), {id}, error);
    };
    return m;
}

std::optional<Mutation> addDependency(const Queries &q, const QString &taskId, const QString &prerequisiteId, qint64 now,
                                      QString *error)
{
    const auto task = q.task(taskId);
    const auto prereq = q.task(prerequisiteId);
    if (!task || !prereq) {
        setError(error, QStringLiteral("task not found"));
        return std::nullopt;
    }
    DependencyGraph graph;
    for (const Dependency &d : q.dependencies())
        graph.add(d.taskId, d.dependsOnId);
    switch (graph.add(taskId, prerequisiteId)) {
    case DependencyGraph::AddResult::Added:
        break;
    case DependencyGraph::AddResult::AlreadyExists:
        setError(error, QStringLiteral("“%1” already waits on “%2”").arg(task->title, prereq->title));
        return std::nullopt;
    case DependencyGraph::AddResult::SelfDependency:
        setError(error, QStringLiteral("a task cannot wait on itself"));
        return std::nullopt;
    case DependencyGraph::AddResult::WouldCreateCycle:
        setError(error, QStringLiteral("“%1” already depends on “%2”, so that would be a loop").arg(prereq->title, task->title));
        return std::nullopt;
    }

    Mutation m;
    m.text = QStringLiteral("“%1” waits on “%2”").arg(task->title, prereq->title);
    m.scopes = {{QStringLiteral("task_dependencies"), QStringLiteral("task_id = ? AND depends_on_task_id = ?"),
                 {taskId, prerequisiteId}}};
    m.apply = [taskId, prerequisiteId, now](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("INSERT INTO task_dependencies (task_id, depends_on_task_id, created_at) VALUES (?, ?, ?)"),
                    {taskId, prerequisiteId, now}, error);
    };
    return m;
}

Mutation removeDependency(const QString &taskId, const QString &prerequisiteId)
{
    Mutation m;
    m.text = QStringLiteral("Remove prerequisite");
    m.scopes = {{QStringLiteral("task_dependencies"), QStringLiteral("task_id = ? AND depends_on_task_id = ?"),
                 {taskId, prerequisiteId}}};
    m.apply = [taskId, prerequisiteId](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("DELETE FROM task_dependencies WHERE task_id = ? AND depends_on_task_id = ?"),
                    {taskId, prerequisiteId}, error);
    };
    return m;
}

Mutation scheduleTask(const QString &scheduleId, const QString &taskId, qint64 start, qint64 end, qint64 now)
{
    Mutation m;
    m.text = QStringLiteral("Schedule task");
    m.scopes = {{QStringLiteral("schedules"), QStringLiteral("id = ?"), {scheduleId}}};
    m.apply = [=](QSqlDatabase db, QString *error) {
        return exec(db,
                    QStringLiteral("INSERT INTO schedules (id, task_id, scheduled_start, scheduled_end, created_at) "
                                   "VALUES (?, ?, ?, ?, ?)"),
                    {scheduleId, taskId, start, std::max(start, end), now}, error);
    };
    return m;
}

std::optional<Mutation> moveSchedule(const Queries &q, const QString &id, qint64 start, qint64 end, bool bump, qint64 now,
                                     QString *error)
{
    const auto block = q.schedule(id);
    if (!block) {
        setError(error, QStringLiteral("schedule not found"));
        return std::nullopt;
    }
    const QString taskId = block->taskId;
    const qint64 oldStart = block->start;

    Mutation m;
    m.text = bump ? QStringLiteral("Bump") : QStringLiteral("Move block");
    m.scopes = {
        {QStringLiteral("schedules"), QStringLiteral("id = ?"), {id}},
        {QStringLiteral("task_history"), QStringLiteral("task_id = ?"), {taskId}},
    };
    m.apply = [=](QSqlDatabase db, QString *error) {
        if (!exec(db,
                  QStringLiteral("UPDATE schedules SET scheduled_start = ?, scheduled_end = ?, "
                                 "bumped_count = bumped_count + ? WHERE id = ?"),
                  {start, std::max(start, end), bump ? 1 : 0, id}, error))
            return false;
        if (!bump)
            return true;
        return addHistory(db, taskId, QStringLiteral("bumped"), now,
                          {{QStringLiteral("from"), oldStart}, {QStringLiteral("to"), start}}, error);
    };
    return m;
}

std::optional<Mutation> completeSchedule(const Queries &q, const QString &id, qint64 now, QString *error)
{
    const auto block = q.schedule(id);
    if (!block) {
        setError(error, QStringLiteral("schedule not found"));
        return std::nullopt;
    }
    const auto task = q.task(block->taskId);

    Mutation done;
    done.text = QStringLiteral("Done");
    done.scopes = {{QStringLiteral("schedules"), QStringLiteral("id = ?"), {id}}};
    done.apply = [id, now](QSqlDatabase db, QString *error) {
        return exec(db,
                    QStringLiteral("UPDATE schedules SET completed = 1, actual_start = COALESCE(actual_start, scheduled_start), "
                                   "actual_end = COALESCE(actual_end, ?) WHERE id = ?"),
                    {now, id}, error);
    };

    // Finishing the block finishes a one-off task; a recurring task keeps going.
    if (task && !task->isRecurring() && !isFinished(task->status)) {
        auto status = setTaskStatus(q, task->id, TaskStatus::Completed, now, error);
        if (!status)
            return std::nullopt;
        return combine(QStringLiteral("Complete “%1”").arg(task->title), {done, *status});
    }
    return done;
}

Mutation deleteSchedule(const QString &id)
{
    Mutation m;
    m.text = QStringLiteral("Unschedule");
    m.scopes = {{QStringLiteral("schedules"), QStringLiteral("id = ?"), {id}}};
    m.apply = [id](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("DELETE FROM schedules WHERE id = ?"), {id}, error);
    };
    return m;
}

std::optional<Mutation> setOccurrenceStatus(const Queries &q, const QString &id, OccurrenceStatus status, qint64 now,
                                            QString *error)
{
    const auto occ = q.occurrence(id);
    if (!occ) {
        setError(error, QStringLiteral("occurrence not found"));
        return std::nullopt;
    }
    const QString taskId = occ->taskId;
    const QString date = occ->date.toString(Qt::ISODate);

    Mutation m;
    m.text = status == OccurrenceStatus::Completed ? QStringLiteral("Done")
        : status == OccurrenceStatus::Skipped      ? QStringLiteral("Skip")
                                                   : QStringLiteral("Undo done");
    m.scopes = {
        {QStringLiteral("task_occurrences"), QStringLiteral("id = ?"), {id}},
        {QStringLiteral("task_history"), QStringLiteral("task_id = ?"), {taskId}},
    };
    m.apply = [=](QSqlDatabase db, QString *error) {
        if (!exec(db, QStringLiteral("UPDATE task_occurrences SET status = ?, completed_at = ? WHERE id = ?"),
                  {nameOf(status), status == OccurrenceStatus::Completed ? QVariant(now) : QVariant(), id}, error))
            return false;
        if (status == OccurrenceStatus::Pending)
            return true;
        return addHistory(db, taskId, status == OccurrenceStatus::Completed ? QStringLiteral("completed") : QStringLiteral("skipped"),
                          now, {{QStringLiteral("occurrence_date"), date}}, error);
    };
    return m;
}

Mutation setOccurrenceTime(const QString &id, const QTime &time)
{
    Mutation m;
    m.text = QStringLiteral("Change time");
    m.scopes = {{QStringLiteral("task_occurrences"), QStringLiteral("id = ?"), {id}}};
    m.apply = [id, time](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("UPDATE task_occurrences SET scheduled_time = ? WHERE id = ?"), {timeText(time), id}, error);
    };
    return m;
}

std::optional<Mutation> startWork(const Queries &q, const QString &workLogId, const QString &taskId, qint64 now, QString *error)
{
    const auto task = q.task(taskId);
    if (!task) {
        setError(error, QStringLiteral("task not found"));
        return std::nullopt;
    }
    const bool markStarted = task->status == TaskStatus::Pending;

    Mutation m;
    m.text = QStringLiteral("Start working on “%1”").arg(task->title);
    m.scopes = {
        {QStringLiteral("work_logs"), QStringLiteral("ended_at IS NULL OR id = ?"), {workLogId}},
        {QStringLiteral("tasks"), QStringLiteral("id = ?"), {taskId}},
        {QStringLiteral("task_history"), QStringLiteral("task_id = ?"), {taskId}},
    };
    m.apply = [=](QSqlDatabase db, QString *error) {
        if (!endActiveWork(db, now, error)
            || !exec(db, QStringLiteral("INSERT INTO work_logs (id, task_id, started_at, created_at) VALUES (?, ?, ?, ?)"),
                     {workLogId, taskId, now, now}, error))
            return false;
        if (!markStarted)
            return true;
        return exec(db, QStringLiteral("UPDATE tasks SET status = 'in_progress', updated_at = ? WHERE id = ?"), {now, taskId}, error)
            && addHistory(db, taskId, QStringLiteral("started"), now, {}, error);
    };
    return m;
}

std::optional<Mutation> placeWork(const Queries &q, const QString &taskId, const QString &workLogId, qint64 now, qint64 dayStart,
                                  qint64 dayEnd)
{
    const auto task = q.task(taskId);
    if (!task || task->isRecurring())
        return std::nullopt;

    std::optional<ScheduleBlock> current, next;
    for (const ScheduleBlock &s : q.schedulesForTask(taskId)) {
        if (s.completed || s.end <= dayStart || s.start >= dayEnd)
            continue;
        if (s.start <= now && now < s.end)
            current = s;
        else if (!next)
            next = s; // earliest: overdue first, then upcoming
    }

    Mutation m;
    m.text = QStringLiteral("Start working on “%1”").arg(task->title);
    m.scopes = {{QStringLiteral("schedules"), QStringLiteral("task_id = ?"), {taskId}}};
    if (current) {
        const QString id = current->id;
        m.apply = [=](QSqlDatabase db, QString *error) {
            return exec(db, QStringLiteral("UPDATE schedules SET actual_start = COALESCE(actual_start, ?), work_log_id = ? WHERE id = ?"),
                        {now, workLogId, id}, error);
        };
    } else if (next) {
        const QString id = next->id;
        const qint64 end = now + (next->end - next->start);
        m.apply = [=](QSqlDatabase db, QString *error) {
            return exec(db,
                        QStringLiteral("UPDATE schedules SET scheduled_start = ?, scheduled_end = ?, actual_start = ?, work_log_id = ? "
                                       "WHERE id = ?"),
                        {now, end, now, workLogId, id}, error);
        };
    } else {
        const QString id = newId();
        const qint64 end = now + qint64(std::max(5, task->estimateMinutes)) * kMinute;
        m.apply = [=](QSqlDatabase db, QString *error) {
            return exec(db,
                        QStringLiteral("INSERT INTO schedules (id, task_id, scheduled_start, scheduled_end, actual_start, work_log_id, "
                                       "created_at) VALUES (?, ?, ?, ?, ?, ?, ?)"),
                        {id, taskId, now, end, now, workLogId, now}, error);
        };
    }
    return m;
}

std::optional<Mutation> recordDone(const Queries &q, const QString &taskId, qint64 now, qint64 dayStart, qint64 dayEnd)
{
    const auto task = q.task(taskId);
    if (!task || task->isRecurring())
        return std::nullopt;

    struct Change {
        QString id;
        qint64 start, end;
    };
    QList<Change> open;
    bool doneToday = false;
    for (const ScheduleBlock &s : q.schedulesForTask(taskId)) {
        if (s.end <= dayStart || s.start >= dayEnd)
            continue;
        if (s.completed) {
            doneToday = true;
            continue;
        }
        // Finished early: the block ends now. Not started yet: it keeps its
        // length and ends now.
        const qint64 start = s.start > now ? now - (s.end - s.start) : s.start;
        open.append({s.id, start, std::min(s.end, now)});
    }

    Mutation m;
    m.text = QStringLiteral("Done");
    m.scopes = {{QStringLiteral("schedules"), QStringLiteral("task_id = ?"), {taskId}}};
    if (!open.isEmpty()) {
        m.apply = [=](QSqlDatabase db, QString *error) {
            for (const Change &c : open) {
                if (!exec(db,
                          QStringLiteral("UPDATE schedules SET completed = 1, scheduled_start = ?, scheduled_end = ?, "
                                         "actual_start = COALESCE(actual_start, ?), actual_end = ? WHERE id = ?"),
                          {c.start, c.end, c.start, now, c.id}, error))
                    return false;
            }
            return true;
        };
        return m;
    }
    if (doneToday)
        return std::nullopt;

    // Nothing planned: show its latest session today, else its estimate.
    qint64 start = now - qint64(std::max(5, task->estimateMinutes)) * kMinute;
    qint64 end = now;
    std::optional<WorkLog> session;
    for (const WorkLog &w : q.workLogsForTask(taskId)) {
        if (w.startedAt >= dayStart && w.startedAt < now && (!session || w.startedAt > session->startedAt))
            session = w;
    }
    if (session) {
        start = session->startedAt;
        end = std::max(start + kMinute, session->endedAt.value_or(now));
    }
    const QString id = newId();
    const QVariant workLogId = session ? QVariant(session->id) : QVariant();
    m.apply = [=](QSqlDatabase db, QString *error) {
        return exec(db,
                    QStringLiteral("INSERT INTO schedules (id, task_id, scheduled_start, scheduled_end, actual_start, actual_end, completed, "
                                   "work_log_id, created_at) VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?)"),
                    {id, taskId, start, end, start, end, workLogId, now}, error);
    };
    return m;
}

std::optional<Mutation> reopenToday(const Queries &q, const QString &taskId, qint64 dayStart, qint64 dayEnd)
{
    QStringList ids;
    for (const ScheduleBlock &s : q.schedulesForTask(taskId)) {
        if (s.completed && s.end > dayStart && s.start < dayEnd)
            ids.append(s.id);
    }
    if (ids.isEmpty())
        return std::nullopt;
    Mutation m;
    m.text = QStringLiteral("Reopen");
    m.scopes = {{QStringLiteral("schedules"), QStringLiteral("task_id = ?"), {taskId}}};
    m.apply = [ids](QSqlDatabase db, QString *error) {
        for (const QString &id : ids) {
            if (!exec(db, QStringLiteral("UPDATE schedules SET completed = 0, actual_end = NULL WHERE id = ?"), {id}, error))
                return false;
        }
        return true;
    };
    return m;
}

std::optional<Mutation> stopWork(const Queries &q, qint64 now, QString *error)
{
    if (!q.activeWorkLog()) {
        setError(error, QStringLiteral("nothing is being worked on"));
        return std::nullopt;
    }
    Mutation m;
    m.text = QStringLiteral("Stop working");
    m.scopes = {{QStringLiteral("work_logs"), QStringLiteral("ended_at IS NULL"), {}}};
    m.apply = [now](QSqlDatabase db, QString *error) { return endActiveWork(db, now, error); };
    return m;
}

std::optional<Mutation> stopWorkAt(const Queries &q, qint64 endedAt, qint64 now, QString *error)
{
    const auto active = q.activeWorkLog();
    if (!active) {
        setError(error, QStringLiteral("nothing is being worked on"));
        return std::nullopt;
    }
    const qint64 end = std::clamp(endedAt, active->startedAt, now);
    Mutation m;
    m.text = QStringLiteral("Stop working");
    m.scopes = {{QStringLiteral("work_logs"), QStringLiteral("ended_at IS NULL"), {}}};
    m.apply = [end](QSqlDatabase db, QString *error) { return endActiveWork(db, end, error); };
    return m;
}

Mutation logWork(const QString &taskId, const QString &workLogId, qint64 start, qint64 end, const QString &blockId, qint64 now)
{
    Mutation m;
    m.text = QStringLiteral("Log work");
    m.scopes = {{QStringLiteral("work_logs"), QStringLiteral("id = ?"), {workLogId}}};
    if (!blockId.isEmpty())
        m.scopes.append({QStringLiteral("schedules"), QStringLiteral("id = ?"), {blockId}});
    m.apply = [=](QSqlDatabase db, QString *error) {
        if (!exec(db,
                  QStringLiteral("INSERT INTO work_logs (id, task_id, started_at, ended_at, duration_minutes, created_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?)"),
                  {workLogId, taskId, start, end, int((end - start + kMinute / 2) / kMinute), now}, error))
            return false;
        return blockId.isEmpty()
            || exec(db,
                    QStringLiteral("UPDATE schedules SET scheduled_start = ?, scheduled_end = ?, actual_start = ?, actual_end = ?, "
                                   "work_log_id = ? WHERE id = ?"),
                    {start, end, start, end, workLogId, blockId}, error);
    };
    return m;
}

std::optional<Mutation> addAction(const Queries &q, const TaskAction &a, qint64 now, QString *error)
{
    if (!q.task(a.taskId)) {
        setError(error, QStringLiteral("task not found"));
        return std::nullopt;
    }
    const QString target = a.target.trimmed();
    if (target.isEmpty()) {
        setError(error, QStringLiteral("nothing to open"));
        return std::nullopt;
    }
    if (a.kind == TaskAction::Kind::Command) {
        const QJsonDocument doc = QJsonDocument::fromJson(target.toUtf8());
        bool ok = doc.isArray() && !doc.array().isEmpty();
        for (const QJsonValue &v : doc.array())
            ok = ok && v.isString() && !v.toString().isEmpty();
        if (!ok) {
            setError(error, QStringLiteral("a command is a list of words, the program first"));
            return std::nullopt;
        }
    }
    int order = 0;
    for (const TaskAction &existing : q.actions(a.taskId))
        order = std::max(order, existing.sortOrder + 1);

    Mutation m;
    m.text = QStringLiteral("Add launcher");
    m.scopes = {{QStringLiteral("task_actions"), QStringLiteral("id = ?"), {a.id}}};
    m.apply = [a, target, order, now](QSqlDatabase db, QString *error) {
        return exec(db,
                    QStringLiteral("INSERT INTO task_actions (id, task_id, kind, target, run_on, sort_order, created_at) "
                                   "VALUES (?, ?, ?, ?, ?, ?, ?)"),
                    {a.id, a.taskId, nameOf(a.kind), target, a.onStart ? QStringLiteral("start") : QStringLiteral("manual"), order, now},
                    error);
    };
    return m;
}

Mutation removeAction(const QString &id)
{
    Mutation m;
    m.text = QStringLiteral("Remove launcher");
    m.scopes = {{QStringLiteral("task_actions"), QStringLiteral("id = ?"), {id}}};
    m.apply = [id](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("DELETE FROM task_actions WHERE id = ?"), {id}, error);
    };
    return m;
}

Mutation saveCategory(const Category &c, bool isNew, qint64 now)
{
    Mutation m;
    m.text = isNew ? QStringLiteral("Add category “%1”").arg(c.name) : QStringLiteral("Edit category “%1”").arg(c.name);
    // Making one category the default clears the flag on the others.
    m.scopes = {{QStringLiteral("categories"), c.isDefault ? QStringLiteral("1") : QStringLiteral("id = ?"),
                 c.isDefault ? QVariantList{} : QVariantList{c.id}}};
    m.apply = [c, isNew, now](QSqlDatabase db, QString *error) {
        if (c.isDefault && !exec(db, QStringLiteral("UPDATE categories SET is_default = 0 WHERE id <> ?"), {c.id}, error))
            return false;
        const QString schedule = jsonText(c.schedule.toJson());
        if (isNew) {
            return exec(db,
                        QStringLiteral("INSERT INTO categories (id, name, schedule, color, is_default, show_on_schedule, "
                                       "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
                        {c.id, c.name.trimmed(), schedule, c.color, c.isDefault ? 1 : 0, c.showOnSchedule ? 1 : 0, now, now},
                        error);
        }
        return exec(db,
                    QStringLiteral("UPDATE categories SET name = ?, schedule = ?, color = ?, is_default = ?, "
                                   "show_on_schedule = ?, updated_at = ? WHERE id = ?"),
                    {c.name.trimmed(), schedule, c.color, c.isDefault ? 1 : 0, c.showOnSchedule ? 1 : 0, now, c.id}, error);
    };
    return m;
}

std::optional<Mutation> deleteCategory(const Queries &q, const QString &id, qint64 now, QString *error)
{
    const auto def = q.defaultCategory();
    if (!def || def->id == id) {
        setError(error, QStringLiteral("the default category cannot be deleted"));
        return std::nullopt;
    }
    const QString defaultId = def->id;

    Mutation m;
    m.text = QStringLiteral("Delete category");
    m.scopes = {
        {QStringLiteral("categories"), QStringLiteral("id = ?"), {id}},
        {QStringLiteral("tasks"), QStringLiteral("category_id = ?"), {id}},
    };
    m.apply = [id, defaultId, now](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("UPDATE tasks SET category_id = ?, updated_at = ? WHERE category_id = ?"),
                    {defaultId, now, id}, error)
            && exec(db, QStringLiteral("DELETE FROM categories WHERE id = ?"), {id}, error);
    };
    return m;
}

Mutation setSetting(const QString &key, const QJsonValue &value)
{
    Mutation m;
    m.text = QStringLiteral("Change setting");
    m.scopes = {{QStringLiteral("settings"), QStringLiteral("key = ?"), {key}}};
    m.apply = [key, value](QSqlDatabase db, QString *error) {
        return exec(db,
                    QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) "
                                   "ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
                    {key, jsonText(value)}, error);
    };
    return m;
}

std::optional<Mutation> saveEvent(const Event &e, bool isNew, const QString &timezone, qint64 now, QString *error)
{
    if (e.title.trimmed().isEmpty()) {
        setError(error, QStringLiteral("an event needs a title"));
        return std::nullopt;
    }
    if (e.allDay ? (!e.startDate.isValid() || !e.endDate.isValid() || e.endDate < e.startDate) : (e.endAt < e.startAt)) {
        setError(error, QStringLiteral("an event can't end before it starts"));
        return std::nullopt;
    }
    QJsonArray tags;
    for (const QString &t : e.blocking.allowedTags) {
        if (!t.trimmed().isEmpty())
            tags.append(t.trimmed());
    }
    const QVariantList values{
        e.title.trimmed(),
        optString(e.description),
        e.allDay ? 1 : 0,
        e.allDay ? QVariant(e.startDate.toString(Qt::ISODate)) : QVariant(),
        e.allDay ? QVariant(e.endDate.toString(Qt::ISODate)) : QVariant(),
        e.allDay ? QVariant() : QVariant(e.startAt),
        e.allDay ? QVariant() : QVariant(e.endAt),
        optString(timezone),
        nameOf(e.blocking.mode),
        jsonText(tags),
        e.color.isEmpty() ? QStringLiteral("#6366f1") : e.color,
        optString(e.location),
    };

    Mutation m;
    m.text = isNew ? QStringLiteral("Add event “%1”").arg(e.title) : QStringLiteral("Edit event “%1”").arg(e.title);
    m.scopes = {{QStringLiteral("events"), QStringLiteral("id = ?"), {e.id}}};
    const QString id = e.id;
    m.apply = [id, values, isNew, now](QSqlDatabase db, QString *error) {
        QVariantList binds = values;
        if (isNew) {
            binds.prepend(id);
            binds << now << now;
            return exec(db,
                        QStringLiteral("INSERT INTO events (id, title, description, is_all_day, start_date, end_date, start_at, "
                                       "end_at, timezone, blocking_mode, allowed_tags, color, location, created_at, updated_at) "
                                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                        binds, error);
        }
        binds << now << id;
        return exec(db,
                    QStringLiteral("UPDATE events SET title = ?, description = ?, is_all_day = ?, start_date = ?, end_date = ?, "
                                   "start_at = ?, end_at = ?, timezone = ?, blocking_mode = ?, allowed_tags = ?, color = ?, "
                                   "location = ?, updated_at = ? WHERE id = ?"),
                    binds, error);
    };
    return m;
}

Mutation deleteEvent(const QString &id)
{
    Mutation m;
    m.text = QStringLiteral("Delete event");
    m.scopes = {
        {QStringLiteral("events"), QStringLiteral("id = ?"), {id}},
        {QStringLiteral("synced_events"), QStringLiteral("event_id = ?"), {id}},
    };
    m.apply = [id](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("DELETE FROM events WHERE id = ?"), {id}, error);
    };
    return m;
}

std::optional<Mutation> logEnergy(const QString &id, int level, qint64 at, const QString &notes, QString *error)
{
    if (level < 1 || level > 10) {
        setError(error, QStringLiteral("energy is 1 to 10"));
        return std::nullopt;
    }
    Mutation m;
    m.text = QStringLiteral("Energy check-in");
    m.scopes = {{QStringLiteral("energy_logs"), QStringLiteral("id = ?"), {id}}};
    m.apply = [=](QSqlDatabase db, QString *error) {
        return exec(db, QStringLiteral("INSERT INTO energy_logs (id, logged_at, energy_level, notes) VALUES (?, ?, ?, ?)"),
                    {id, at, level, optString(notes)}, error);
    };
    return m;
}

std::optional<Mutation> saveHealth(const Queries &q, const HealthLog &log, qint64 now, QString *error)
{
    if (!log.date.isValid()) {
        setError(error, QStringLiteral("a health log needs a date"));
        return std::nullopt;
    }
    const auto inRange = [](const std::optional<int> &v) { return !v || (*v >= 0 && *v <= 100); };
    if (!inRange(log.sleepScore) || !inRange(log.readinessScore)) {
        setError(error, QStringLiteral("scores are 0 to 100"));
        return std::nullopt;
    }
    const auto existing = q.healthLog(log.date);
    const QString id = existing ? existing->id : (log.id.isEmpty() ? newId() : log.id);
    const QString date = log.date.toString(Qt::ISODate);
    const auto opt = [](const auto &v) { return v ? QVariant(*v) : QVariant(); };
    const QVariantList values{opt(log.sleepScore), opt(log.readinessScore), opt(log.hoursSlept), optString(log.notes)};

    Mutation m;
    m.text = QStringLiteral("Health check-in");
    m.scopes = {{QStringLiteral("health_logs"), QStringLiteral("log_date = ?"), {date}}};
    m.apply = [id, date, values, now](QSqlDatabase db, QString *error) {
        QVariantList binds{id, date};
        binds += values;
        binds << now << now;
        return exec(db,
                    QStringLiteral("INSERT INTO health_logs (id, log_date, sleep_score, readiness_score, hours_slept, notes, "
                                   "created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                                   "ON CONFLICT(log_date) DO UPDATE SET sleep_score = excluded.sleep_score, "
                                   "readiness_score = excluded.readiness_score, hours_slept = excluded.hours_slept, "
                                   "notes = excluded.notes, updated_at = excluded.updated_at"),
                    binds, error);
    };
    return m;
}

} // namespace ops

int materializeOccurrences(QSqlDatabase db, const QList<Task> &tasks, QDate from, QDate to, qint64 now)
{
    int inserted = 0;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO task_occurrences (id, task_id, occurrence_date, created_at) "
                             "VALUES (?, ?, ?, ?)"));
    for (const Task &t : tasks) {
        if (!t.recurrence || isFinished(t.status))
            continue;
        for (const QDate &d : t.recurrence->occurrencesBetween(from, to)) {
            q.addBindValue(newId());
            q.addBindValue(t.id);
            q.addBindValue(d.toString(Qt::ISODate));
            q.addBindValue(now);
            if (q.exec())
                inserted += q.numRowsAffected();
        }
    }
    return inserted;
}

} // namespace tumbler
