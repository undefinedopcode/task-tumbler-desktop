#include "worktime.h"

#include <QJsonObject>

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;
constexpr qint64 kKeep = 36 * 60 * kMinute;
} // namespace

std::optional<WorkGuess> guessWork(qint64 start, int plannedMinutes, qint64 now)
{
    if (now - start < kMinute)
        return std::nullopt;
    const int planned = plannedMinutes > 0 ? plannedMinutes : 15;
    const qint64 allowed = std::max(qint64(planned) * kMinute * 3 / 2, qint64(planned + 15) * kMinute);
    return WorkGuess{start, now, planned, now - start <= allowed};
}

QJsonArray toJson(const QList<TimeCheck> &checks)
{
    QJsonArray out;
    for (const TimeCheck &c : checks) {
        QJsonObject o{
            {QStringLiteral("id"), c.id},
            {QStringLiteral("taskId"), c.taskId},
            {QStringLiteral("title"), c.title},
            {QStringLiteral("start"), double(c.start)},
            {QStringLiteral("doneAt"), double(c.doneAt)},
            {QStringLiteral("planned"), c.plannedMinutes},
        };
        if (!c.blockId.isEmpty())
            o.insert(QStringLiteral("blockId"), c.blockId);
        out.append(o);
    }
    return out;
}

QList<TimeCheck> timeChecksFromJson(const QJsonArray &array)
{
    QList<TimeCheck> out;
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        TimeCheck c;
        c.id = o.value(QLatin1String("id")).toString();
        c.taskId = o.value(QLatin1String("taskId")).toString();
        if (c.id.isEmpty() || c.taskId.isEmpty())
            continue;
        c.title = o.value(QLatin1String("title")).toString();
        c.blockId = o.value(QLatin1String("blockId")).toString();
        c.start = qint64(o.value(QLatin1String("start")).toDouble());
        c.doneAt = qint64(o.value(QLatin1String("doneAt")).toDouble());
        c.plannedMinutes = o.value(QLatin1String("planned")).toInt();
        out.append(c);
    }
    return out;
}

QList<TimeCheck> freshChecks(QList<TimeCheck> checks, qint64 now)
{
    checks.erase(std::remove_if(checks.begin(), checks.end(), [now](const TimeCheck &c) { return now - c.doneAt > kKeep; }), checks.end());
    return checks;
}

std::pair<qint64, qint64> workSpan(const TimeCheck &check, int minutes)
{
    const qint64 length = qint64(minutes) * kMinute;
    if (check.start + length <= check.doneAt)
        return {check.start, check.start + length};
    return {check.doneAt - length, check.doneAt};
}

} // namespace tumbler
