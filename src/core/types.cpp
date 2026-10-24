#include "types.h"

#include <array>
#include <utility>

namespace tumbler {

namespace {

template<typename E, std::size_t N>
QString lookupName(const std::array<std::pair<E, const char *>, N> &table, E value)
{
    for (const auto &[e, name] : table) {
        if (e == value)
            return QString::fromLatin1(name);
    }
    return {};
}

template<typename E, std::size_t N>
std::optional<E> lookupValue(const std::array<std::pair<E, const char *>, N> &table, const QString &s)
{
    for (const auto &[e, name] : table) {
        if (s == QLatin1String(name))
            return e;
    }
    return std::nullopt;
}

constexpr std::array<std::pair<TaskStatus, const char *>, 4> kTaskStatus{{
    {TaskStatus::Pending, "pending"},
    {TaskStatus::InProgress, "in_progress"},
    {TaskStatus::Completed, "completed"},
    {TaskStatus::Cancelled, "cancelled"},
}};

constexpr std::array<std::pair<Priority, const char *>, 4> kPriority{{
    {Priority::Urgent, "urgent"},
    {Priority::High, "high"},
    {Priority::Medium, "medium"},
    {Priority::Low, "low"},
}};

constexpr std::array<std::pair<Energy, const char *>, 4> kEnergy{{
    {Energy::Low, "low"},
    {Energy::Medium, "medium"},
    {Energy::High, "high"},
    {Energy::Peak, "peak"},
}};

constexpr std::array<std::pair<OccurrenceStatus, const char *>, 3> kOccurrenceStatus{{
    {OccurrenceStatus::Pending, "pending"},
    {OccurrenceStatus::Completed, "completed"},
    {OccurrenceStatus::Skipped, "skipped"},
}};

} // namespace

QString nameOf(TaskStatus s) { return lookupName(kTaskStatus, s); }
QString nameOf(Priority p) { return lookupName(kPriority, p); }
QString nameOf(Energy e) { return lookupName(kEnergy, e); }
QString nameOf(OccurrenceStatus s) { return lookupName(kOccurrenceStatus, s); }

std::optional<TaskStatus> taskStatusFromString(const QString &s) { return lookupValue(kTaskStatus, s); }
std::optional<Priority> priorityFromString(const QString &s) { return lookupValue(kPriority, s); }
std::optional<Energy> energyFromString(const QString &s) { return lookupValue(kEnergy, s); }
std::optional<OccurrenceStatus> occurrenceStatusFromString(const QString &s) { return lookupValue(kOccurrenceStatus, s); }

bool isFinished(TaskStatus s)
{
    return s == TaskStatus::Completed || s == TaskStatus::Cancelled;
}

int weekday0(QDate date)
{
    return date.dayOfWeek() % 7;
}

} // namespace tumbler
