// AppController: time worked on something ticked off without pressing Start,
// logged when it's about what was planned, asked about when it isn't.

#include "appcontroller.h"
#include "operations.h"

#include <QDateTime>

namespace tumbler {

namespace {

const QString kSetting = QStringLiteral("time_checks");
constexpr qint64 kMinute = 60 * 1000;

} // namespace

AppController::Untracked AppController::untracked(const QString &taskId, const QString &blockId, qint64 start, int plannedMinutes,
                                                  qint64 at) const
{
    Untracked u;
    const Queries q = queries();
    const auto task = q.task(taskId);
    if (!task)
        return u;
    // Started with the timer (since it was due, or still running): the
    // timer has it.
    for (const WorkLog &w : q.workLogsForTask(taskId)) {
        if (!w.endedAt || w.startedAt >= start - 30 * kMinute)
            return u;
    }
    const auto guess = guessWork(start, plannedMinutes, at);
    if (!guess)
        return u;
    if (guess->sure) {
        u.log = ops::logWork(taskId, newId(), guess->start, guess->end, blockId, at);
        u.minutes = int((guess->end - guess->start + kMinute / 2) / kMinute);
    } else {
        u.ask = TimeCheck{newId(), taskId, task->title, blockId, start, at, guess->plannedMinutes};
    }
    return u;
}

void AppController::askUntracked(const Untracked &u)
{
    if (!u.ask)
        return;
    QList<TimeCheck> checks = freshChecks(timeChecksFromJson(queries().setting(kSetting, QJsonArray()).toArray()), now());
    // One question per task: the latest tick-off.
    checks.erase(std::remove_if(checks.begin(), checks.end(), [&u](const TimeCheck &c) { return c.taskId == u.ask->taskId; }), checks.end());
    checks.append(*u.ask);
    applyExternal(ops::setSetting(kSetting, toJson(checks)));
    for (const QVariant &v : timeChecks()) {
        if (v.toMap().value(QStringLiteral("id")).toString() == u.ask->id)
            Q_EMIT timeCheckNeeded(v.toMap());
    }
}

QVariantList AppController::timeChecks() const
{
    QVariantList out;
    for (const TimeCheck &c : freshChecks(timeChecksFromJson(queries().setting(kSetting, QJsonArray()).toArray()), now())) {
        out.append(QVariantMap{
            {QStringLiteral("id"), c.id},
            {QStringLiteral("taskId"), c.taskId},
            {QStringLiteral("title"), c.title},
            {QStringLiteral("start"), c.start},
            {QStringLiteral("doneAt"), c.doneAt},
            {QStringLiteral("planned"), c.plannedMinutes},
            {QStringLiteral("elapsed"), int((c.doneAt - c.start + kMinute / 2) / kMinute)},
        });
    }
    return out;
}

bool AppController::resolveTimeCheck(const QString &id, int minutes)
{
    QList<TimeCheck> checks = timeChecksFromJson(queries().setting(kSetting, QJsonArray()).toArray());
    const auto it = std::find_if(checks.cbegin(), checks.cend(), [&id](const TimeCheck &c) { return c.id == id; });
    if (it == checks.cend())
        return false;
    const TimeCheck check = *it;
    checks.erase(it);
    const Mutation answered = ops::setSetting(kSetting, toJson(checks));
    if (minutes <= 0)
        return applyExternal(answered);

    const auto [start, end] = workSpan(check, std::min(minutes, 24 * 60));
    // The block it was ticked off on moves to cover it, if it's still there.
    const auto block = queries().schedule(check.blockId);
    const QString blockId = block && block->taskId == check.taskId ? check.blockId : QString();
    const bool ok = apply(combine(QStringLiteral("Log %1 min on “%2”").arg(minutes).arg(check.title),
                                  {ops::logWork(check.taskId, newId(), start, end, blockId, now()), answered}));
    if (ok)
        Q_EMIT message(QStringLiteral("Logged %1 min on “%2”").arg(minutes).arg(check.title));
    return ok;
}

} // namespace tumbler
