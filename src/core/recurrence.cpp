#include "recurrence.h"
#include "types.h"

#include <QJsonArray>
#include <algorithm>

namespace tumbler {

namespace {

QDate startOfWeek(QDate date)
{
    return date.addDays(-weekday0(date));
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

std::optional<RecurrencePattern> RecurrencePattern::fromJson(const QJsonObject &json, QString *error)
{
    RecurrencePattern p;

    const QString type = json.value(QLatin1String("type")).toString();
    if (type == QLatin1String("daily")) {
        p.type = Type::Daily;
    } else if (type == QLatin1String("weekly")) {
        p.type = Type::Weekly;
    } else {
        setError(error, QStringLiteral("unknown recurrence type '%1'").arg(type));
        return std::nullopt;
    }

    p.interval = json.value(QLatin1String("interval")).toInt(1);

    for (const QJsonValue &v : json.value(QLatin1String("days")).toArray())
        p.days.append(v.toInt(-1));
    std::sort(p.days.begin(), p.days.end());
    p.days.erase(std::unique(p.days.begin(), p.days.end()), p.days.end());

    // The web app wrote full ISO timestamps here as often as plain dates; the
    // calendar date is the leading YYYY-MM-DD either way.
    const auto readDate = [&json](const char *key) {
        return QDate::fromString(json.value(QLatin1String(key)).toString().left(10), Qt::ISODate);
    };
    p.startDate = readDate("startDate");
    if (const QDate end = readDate("endDate"); end.isValid())
        p.endDate = end;

    if (!p.isValid(error))
        return std::nullopt;
    return p;
}

QJsonObject RecurrencePattern::toJson() const
{
    QJsonObject json;
    json.insert(QLatin1String("type"), type == Type::Daily ? QLatin1String("daily") : QLatin1String("weekly"));
    json.insert(QLatin1String("interval"), interval);
    if (type == Type::Weekly) {
        QJsonArray arr;
        for (int d : days)
            arr.append(d);
        json.insert(QLatin1String("days"), arr);
    }
    if (startDate.isValid())
        json.insert(QLatin1String("startDate"), startDate.toString(Qt::ISODate));
    if (endDate)
        json.insert(QLatin1String("endDate"), endDate->toString(Qt::ISODate));
    return json;
}

bool RecurrencePattern::isValid(QString *error) const
{
    if (interval < 1) {
        setError(error, QStringLiteral("interval must be at least 1"));
        return false;
    }
    if (type == Type::Weekly) {
        if (days.isEmpty()) {
            setError(error, QStringLiteral("weekly recurrence needs at least one day"));
            return false;
        }
        for (int d : days) {
            if (d < 0 || d > 6) {
                setError(error, QStringLiteral("weekday %1 is out of range 0-6").arg(d));
                return false;
            }
        }
    }
    if (endDate && startDate.isValid() && *endDate < startDate) {
        setError(error, QStringLiteral("endDate is before startDate"));
        return false;
    }
    return true;
}

bool RecurrencePattern::occursOn(QDate date) const
{
    if (!date.isValid() || !startDate.isValid() || date < startDate)
        return false;
    if (endDate && date > *endDate)
        return false;

    switch (type) {
    case Type::Daily:
        return startDate.daysTo(date) % interval == 0;
    case Type::Weekly: {
        if (!days.contains(weekday0(date)))
            return false;
        const qint64 weeks = startOfWeek(startDate).daysTo(startOfWeek(date)) / 7;
        return weeks % interval == 0;
    }
    }
    return false;
}

QList<QDate> RecurrencePattern::occurrencesBetween(QDate from, QDate to) const
{
    QList<QDate> result;
    if (!from.isValid() || !to.isValid() || !startDate.isValid() || !isValid())
        return result;

    QDate first = std::max(from, startDate);
    QDate last = endDate ? std::min(to, *endDate) : to;

    if (type == Type::Daily) {
        // Jump straight to the first aligned day rather than testing each one.
        const qint64 offset = startDate.daysTo(first) % interval;
        if (offset != 0)
            first = first.addDays(interval - offset);
        for (QDate d = first; d <= last; d = d.addDays(interval))
            result.append(d);
        return result;
    }

    for (QDate d = first; d <= last; d = d.addDays(1)) {
        if (occursOn(d))
            result.append(d);
    }
    return result;
}

} // namespace tumbler
