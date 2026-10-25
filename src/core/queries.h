#pragma once

#include "entities.h"

#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QSqlDatabase>
#include <optional>

namespace tumbler {

// Read access to the database. Writes go through Mutation (mutations.h) so
// every change is undoable and announces which tables it touched.
class Queries
{
public:
    explicit Queries(QSqlDatabase db);

    QList<Task> tasks() const;
    std::optional<Task> task(const QString &id) const;
    QList<Category> categories() const;
    std::optional<Category> defaultCategory() const;
    QList<Dependency> dependencies() const;

    // Schedule blocks overlapping [from, to).
    QList<ScheduleBlock> schedules(qint64 from, qint64 to) const;
    QList<ScheduleBlock> schedulesForTask(const QString &taskId) const;
    std::optional<ScheduleBlock> schedule(const QString &id) const;

    QList<Occurrence> occurrences(QDate from, QDate to) const;
    std::optional<Occurrence> occurrence(const QString &id) const;

    // Timed events overlapping [from, to) plus all-day events touching the
    // local dates in that range.
    QList<Event> events(qint64 from, qint64 to, QDate firstDay, QDate lastDay) const;

    std::optional<Event> event(const QString &id) const;

    QList<EnergyLog> energyLogs(qint64 from, qint64 to) const;
    // The most recent check-in at or after `since`, if any.
    std::optional<EnergyLog> latestEnergy(qint64 since) const;
    QList<HealthLog> healthLogs(QDate from, QDate to) const;
    std::optional<HealthLog> healthLog(QDate date) const;

    QList<TaskAction> actions(const QString &taskId) const;
    std::optional<TaskAction> action(const QString &id) const;

    std::optional<WorkLog> activeWorkLog() const;
    QList<WorkLog> workLogsForTask(const QString &taskId) const;
    // Minutes logged per task, finished sessions only.
    QHash<QString, int> loggedMinutes() const;

    QJsonValue setting(const QString &key, const QJsonValue &fallback = {}) const;
    bool isEmpty() const; // no tasks and no categories
    QSqlDatabase db() const { return m_db; }

private:
    QSqlDatabase m_db;
};

} // namespace tumbler
