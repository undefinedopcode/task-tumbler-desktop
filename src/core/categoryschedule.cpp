#include "categoryschedule.h"
#include "types.h"

namespace tumbler {

CategorySchedule CategorySchedule::fromJson(const QJsonObject &json)
{
    CategorySchedule s;
    for (int day = 0; day < 7; ++day) {
        const QJsonObject entry = json.value(QString::number(day)).toObject();
        if (!entry.value(QLatin1String("enabled")).toBool())
            continue;
        const QTime start = QTime::fromString(entry.value(QLatin1String("start")).toString(), QStringLiteral("HH:mm"));
        const QTime end = QTime::fromString(entry.value(QLatin1String("end")).toString(), QStringLiteral("HH:mm"));
        // An enabled day without usable times counts as off, as it did in the
        // web app's prompt builder.
        if (start.isValid() && end.isValid() && start < end)
            s.m_days[day] = TimeWindow{start, end};
    }
    return s;
}

CategorySchedule CategorySchedule::weekdays(QTime start, QTime end)
{
    CategorySchedule s;
    for (int day = 1; day <= 5; ++day)
        s.m_days[day] = TimeWindow{start, end};
    return s;
}

CategorySchedule CategorySchedule::everyDay(QTime start, QTime end)
{
    CategorySchedule s;
    for (int day = 0; day < 7; ++day)
        s.m_days[day] = TimeWindow{start, end};
    return s;
}

QJsonObject CategorySchedule::toJson() const
{
    QJsonObject json;
    for (int day = 0; day < 7; ++day) {
        QJsonObject entry;
        entry.insert(QLatin1String("enabled"), m_days[day].has_value());
        if (m_days[day]) {
            entry.insert(QLatin1String("start"), m_days[day]->start.toString(QStringLiteral("HH:mm")));
            entry.insert(QLatin1String("end"), m_days[day]->end.toString(QStringLiteral("HH:mm")));
        }
        json.insert(QString::number(day), entry);
    }
    return json;
}

std::optional<TimeWindow> CategorySchedule::windowFor(QDate date) const
{
    if (!date.isValid())
        return std::nullopt;
    return windowForWeekday(weekday0(date));
}

std::optional<TimeWindow> CategorySchedule::windowForWeekday(int day) const
{
    if (day < 0 || day > 6)
        return std::nullopt;
    return m_days[day];
}

void CategorySchedule::setWindow(int day, std::optional<TimeWindow> window)
{
    if (day >= 0 && day <= 6)
        m_days[day] = window;
}

} // namespace tumbler
