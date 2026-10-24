#pragma once

#include <QDate>
#include <QString>
#include <optional>

namespace tumbler {

enum class TaskStatus { Pending, InProgress, Completed, Cancelled };
enum class Priority { Urgent, High, Medium, Low };
enum class Energy { Low, Medium, High, Peak };
enum class OccurrenceStatus { Pending, Completed, Skipped };

// String forms match the database CHECK constraints and the web app's API.
QString nameOf(TaskStatus s);
QString nameOf(Priority p);
QString nameOf(Energy e);
QString nameOf(OccurrenceStatus s);

std::optional<TaskStatus> taskStatusFromString(const QString &s);
std::optional<Priority> priorityFromString(const QString &s);
std::optional<Energy> energyFromString(const QString &s);
std::optional<OccurrenceStatus> occurrenceStatusFromString(const QString &s);

// A task counts as finished for dependency and parent-completion purposes when
// it is completed or cancelled.
bool isFinished(TaskStatus s);

// Day-of-week using the web app's convention: 0 = Sunday ... 6 = Saturday.
// QDate::dayOfWeek() is 1 = Monday ... 7 = Sunday. Always go through this
// helper; never derive a weekday from a UTC timestamp of a local date.
int weekday0(QDate date);

} // namespace tumbler
