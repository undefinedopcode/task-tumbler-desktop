#pragma once

#include "entities.h"
#include "mutation.h"
#include "queries.h"

#include <QVariantMap>
#include <optional>

namespace tumbler {

QString newId();

// Builders for every write the app makes. Each returns a Mutation whose
// scopes cover all rows it touches, so MutationRunner can undo it. Builders
// read current state through Queries where the scopes depend on it, and
// return nullopt with `error` set when the change is not allowed.
namespace ops {

Mutation createTask(const Task &task, qint64 now);

// Editable task columns by name: title, description, priority, due_date,
// time_estimate_minutes, complexity, energy_required, scheduled_time,
// is_fixed_time, parent_task_id, category_id, tags (string list), color,
// recurrence (JSON object or null), sort_order. Status has its own builder.
std::optional<Mutation> updateTask(const Queries &q, const QString &id, const QVariantMap &fields, qint64 now,
                                   QString *error = nullptr);

// Status change with the web app's side effects: history entries, and the
// parent completing once every subtask is finished. Completing a task also
// ends a work session running on it.
std::optional<Mutation> setTaskStatus(const Queries &q, const QString &id, TaskStatus status, qint64 now,
                                      QString *error = nullptr);

Mutation deleteTask(const QString &id);

std::optional<Mutation> addDependency(const Queries &q, const QString &taskId, const QString &prerequisiteId,
                                      qint64 now, QString *error = nullptr);
Mutation removeDependency(const QString &taskId, const QString &prerequisiteId);

Mutation scheduleTask(const QString &scheduleId, const QString &taskId, qint64 start, qint64 end, qint64 now);
// Moving counts as a bump (bumped_count + history) when `bump` is set.
std::optional<Mutation> moveSchedule(const Queries &q, const QString &id, qint64 start, qint64 end, bool bump, qint64 now,
                                     QString *error = nullptr);
// Marks the block done and, for one-off tasks, completes the task too.
std::optional<Mutation> completeSchedule(const Queries &q, const QString &id, qint64 now, QString *error = nullptr);
Mutation deleteSchedule(const QString &id);

std::optional<Mutation> setOccurrenceStatus(const Queries &q, const QString &id, OccurrenceStatus status, qint64 now,
                                            QString *error = nullptr);
Mutation setOccurrenceTime(const QString &id, const QTime &time);

// Starts a work session, ending any other first; a pending task becomes
// in progress.
std::optional<Mutation> startWork(const Queries &q, const QString &workLogId, const QString &taskId, qint64 now,
                                  QString *error = nullptr);
std::optional<Mutation> stopWork(const Queries &q, qint64 now, QString *error = nullptr);

Mutation saveCategory(const Category &category, bool isNew, qint64 now);
// Tasks in the deleted category move to the default one, which cannot itself
// be deleted.
std::optional<Mutation> deleteCategory(const Queries &q, const QString &id, qint64 now, QString *error = nullptr);

Mutation setSetting(const QString &key, const QJsonValue &value);

} // namespace ops

// Creates the occurrence rows recurring tasks are due for in [from, to].
// Idempotent and deliberately not undoable: occurrences are derived data.
int materializeOccurrences(QSqlDatabase db, const QList<Task> &tasks, QDate from, QDate to, qint64 now);

} // namespace tumbler
