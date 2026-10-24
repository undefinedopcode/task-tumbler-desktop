#pragma once

#include "blocking.h"
#include "categoryschedule.h"
#include "recurrence.h"
#include "types.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QTime>
#include <optional>

namespace tumbler {

// Plain rows as the rest of the app sees them. Instants are UTC milliseconds,
// dates and times are local wall-clock values, matching the schema.

struct Task {
    QString id;
    QString title;
    QString description;
    TaskStatus status = TaskStatus::Pending;
    Priority priority = Priority::Medium;
    QDate dueDate;
    int estimateMinutes = 30;
    int complexity = 3;
    Energy energy = Energy::Medium;
    std::optional<RecurrencePattern> recurrence;
    QTime scheduledTime;
    bool fixedTime = false;
    QString parentId;
    QString categoryId;
    QStringList tags;
    QString color;
    std::optional<int> sortOrder;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;

    bool isRecurring() const { return recurrence.has_value(); }
};

struct Category {
    QString id;
    QString name;
    CategorySchedule schedule;
    QString color = QStringLiteral("#6366f1");
    bool isDefault = false;
    bool showOnSchedule = false;
};

struct ScheduleBlock {
    QString id;
    QString taskId;
    qint64 start = 0;
    qint64 end = 0;
    std::optional<qint64> actualStart;
    std::optional<qint64> actualEnd;
    bool completed = false;
    int bumpedCount = 0;
    QString notes;
    QString workLogId;
};

struct Occurrence {
    QString id;
    QString taskId;
    QDate date;
    OccurrenceStatus status = OccurrenceStatus::Pending;
    std::optional<qint64> completedAt;
    QTime scheduledTime; // override; invalid means use the task's
};

struct WorkLog {
    QString id;
    QString taskId;
    qint64 startedAt = 0;
    std::optional<qint64> endedAt;
    std::optional<int> durationMinutes;
    QString notes;
};

struct Event {
    QString id;
    QString title;
    QString description;
    bool allDay = false;
    QDate startDate; // all-day
    QDate endDate;
    qint64 startAt = 0; // timed
    qint64 endAt = 0;
    EventBlockingRule blocking;
    QString color;
    QString location;
};

struct EnergyLog {
    QString id;
    qint64 loggedAt = 0;
    int level = 5; // 1-10
    QString notes;
};

struct HealthLog {
    QString id;
    QDate date;
    std::optional<int> sleepScore;     // 0-100
    std::optional<int> readinessScore; // 0-100
    std::optional<double> hoursSlept;
    QString notes;
};

struct Dependency {
    QString taskId;
    QString dependsOnId;
};

} // namespace tumbler
