// AppController: stats. Where the time went and how estimates hold up.

#include "appcontroller.h"
#include "stats.h"

#include <QLocale>

namespace tumbler {

namespace {

QVariantMap toVariant(const stats::Calibration &c, const QString &key)
{
    return {{key, c.label}, {QStringLiteral("tasks"), c.tasks}, {QStringLiteral("ratio"), c.ratio}};
}

} // namespace

QVariantMap AppController::stats(int days) const
{
    const Queries q = queries();
    const QTimeZone z = zone();
    const QDate today = QDateTime::currentDateTime(z).date();
    QDate from = today.addDays(1 - std::max(1, days));
    if (days <= 0)
        from = std::min(today, stats::firstActivity(q, z).value_or(today));
    const stats::Summary s = stats::summarize(q, from, today, z);

    // A bar a day, or a bar a week (from Monday) once that gets too thin.
    QVariantList bars;
    const bool weekly = s.days.size() > 62;
    const QString dayFormat = s.days.size() <= 14 ? QStringLiteral("ddd d") : QStringLiteral("d MMM");
    for (const stats::Day &d : s.days) {
        if (weekly && !bars.isEmpty() && d.date.dayOfWeek() != Qt::Monday) {
            QVariantMap last = bars.takeLast().toMap();
            last[QStringLiteral("workedMinutes")] = last.value(QStringLiteral("workedMinutes")).toInt() + d.workedMinutes;
            last[QStringLiteral("finished")] = last.value(QStringLiteral("finished")).toInt() + d.finished;
            bars.append(last);
            continue;
        }
        bars.append(QVariantMap{
            {QStringLiteral("date"), d.date.toString(Qt::ISODate)},
            {QStringLiteral("label"), QLocale().toString(d.date, weekly ? QStringLiteral("d MMM") : dayFormat)},
            {QStringLiteral("workedMinutes"), d.workedMinutes},
            {QStringLiteral("finished"), d.finished},
        });
    }

    QVariantList hours;
    for (int h = 0; h < 24; ++h) {
        hours.append(QVariantMap{
            {QStringLiteral("hour"), h},
            {QStringLiteral("minutes"), s.minutesByHour[h]},
            {QStringLiteral("energy"), s.energyByHour[h] ? QVariant(*s.energyByHour[h]) : QVariant()},
        });
    }

    QVariantList tasks;
    for (const stats::TaskTime &t : s.tasks) {
        tasks.append(QVariantMap{
            {QStringLiteral("id"), t.taskId},
            {QStringLiteral("title"), t.title},
            {QStringLiteral("estimate"), t.estimateMinutes},
            {QStringLiteral("worked"), t.workedMinutes},
            {QStringLiteral("sessions"), t.sessions},
            {QStringLiteral("finished"), t.finished},
        });
    }

    QVariantList bySize;
    for (const stats::Calibration &c : s.bySize)
        bySize.append(toVariant(c, QStringLiteral("size")));
    QVariantList byCategory;
    for (const stats::Calibration &c : s.byCategory)
        byCategory.append(toVariant(c, QStringLiteral("name")));

    QVariantList moving;
    for (const stats::Moving &m : s.moving)
        moving.append(QVariantMap{{QStringLiteral("id"), m.taskId}, {QStringLiteral("title"), m.title}, {QStringLiteral("bumps"), m.bumps}});

    return {
        {QStringLiteral("from"), s.from.toString(Qt::ISODate)},
        {QStringLiteral("to"), s.to.toString(Qt::ISODate)},
        {QStringLiteral("workedMinutes"), s.workedMinutes},
        {QStringLiteral("sessions"), s.sessions},
        {QStringLiteral("averageSessionMinutes"), s.sessions ? int(std::lround(double(s.workedMinutes) / s.sessions)) : 0},
        {QStringLiteral("longestSessionMinutes"), s.longestSessionMinutes},
        {QStringLiteral("finishedTasks"), s.finishedTasks},
        {QStringLiteral("finishedRoutines"), s.finishedRoutines},
        {QStringLiteral("bars"), bars},
        {QStringLiteral("weekly"), weekly},
        {QStringLiteral("hours"), hours},
        {QStringLiteral("tasks"), tasks},
        {QStringLiteral("estimates"),
         QVariantMap{
             {QStringLiteral("tasks"), s.overall.tasks},
             {QStringLiteral("ratio"), s.overall.ratio},
             {QStringLiteral("estimated"), s.overall.estimatedMinutes},
             {QStringLiteral("worked"), s.overall.workedMinutes},
             {QStringLiteral("bySize"), bySize},
             {QStringLiteral("byCategory"), byCategory},
         }},
        {QStringLiteral("moving"), moving},
        {QStringLiteral("planWithPace"), q.setting(QStringLiteral("plan_with_pace"), true).toBool()},
        {QStringLiteral("paceActive"), !pace().isEmpty()},
    };
}

} // namespace tumbler
