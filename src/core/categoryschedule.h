#pragma once

#include <QDate>
#include <QJsonObject>
#include <QTime>
#include <array>
#include <optional>

namespace tumbler {

struct TimeWindow {
    QTime start;
    QTime end;
    bool operator==(const TimeWindow &) const = default;
};

// Per-weekday working window for a category, stored as JSON in
// categories.schedule with keys "0" (Sunday) .. "6" (Saturday):
//   {"1": {"enabled": true, "start": "09:00", "end": "17:00"}, "0": {"enabled": false}, ...}
class CategorySchedule
{
public:
    static CategorySchedule fromJson(const QJsonObject &json);
    // Mon-Fri between the given times, weekends off: the web app's default.
    static CategorySchedule weekdays(QTime start, QTime end);
    // Every day between the given times.
    static CategorySchedule everyDay(QTime start, QTime end);
    // What a new database starts with, and what tasks without a usable
    // category get: any day, 06:00-23:00, so nothing is ever unschedulable.
    static CategorySchedule anytime() { return everyDay(QTime(6, 0), QTime(23, 0)); }

    QJsonObject toJson() const;

    // The window for that calendar date, or nullopt if the category is off.
    // Uses the local date's own weekday; see weekday0().
    std::optional<TimeWindow> windowFor(QDate date) const;
    std::optional<TimeWindow> windowForWeekday(int weekday0) const;

    void setWindow(int weekday0, std::optional<TimeWindow> window);

private:
    std::array<std::optional<TimeWindow>, 7> m_days;
};

} // namespace tumbler
