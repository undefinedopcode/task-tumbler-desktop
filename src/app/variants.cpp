#include "variants.h"

#include <QLocale>

namespace tumbler {

namespace {

QString dayName(int weekday0, bool shortForm)
{
    // QLocale counts Monday = 1 ... Sunday = 7.
    return QLocale().dayName(weekday0 == 0 ? 7 : weekday0, shortForm ? QLocale::ShortFormat : QLocale::LongFormat);
}

QString kindName(AgendaItem::Kind k)
{
    switch (k) {
    case AgendaItem::Kind::Schedule:
        return QStringLiteral("schedule");
    case AgendaItem::Kind::Occurrence:
        return QStringLiteral("occurrence");
    case AgendaItem::Kind::Event:
        return QStringLiteral("event");
    }
    return {};
}

QString stateName(AgendaItem::State s)
{
    switch (s) {
    case AgendaItem::State::Upcoming:
        return QStringLiteral("upcoming");
    case AgendaItem::State::Now:
        return QStringLiteral("now");
    case AgendaItem::State::Overdue:
        return QStringLiteral("overdue");
    case AgendaItem::State::Done:
        return QStringLiteral("done");
    case AgendaItem::State::Skipped:
        return QStringLiteral("skipped");
    }
    return {};
}

} // namespace

QString describeRecurrence(const RecurrencePattern &p)
{
    if (p.type == RecurrencePattern::Type::Daily)
        return p.interval == 1 ? QStringLiteral("Every day") : QStringLiteral("Every %1 days").arg(p.interval);

    QString days;
    const bool singular = p.interval == 1;
    if (p.days == QList<int>{1, 2, 3, 4, 5})
        days = singular ? QStringLiteral("weekday") : QStringLiteral("weekdays");
    else if (p.days == QList<int>{0, 6})
        days = singular ? QStringLiteral("weekend") : QStringLiteral("weekends");
    else if (p.days.size() == 7)
        days = QStringLiteral("every day");
    else {
        QStringList names;
        for (int d : p.days)
            names.append(dayName(d, p.days.size() > 2));
        days = names.join(QStringLiteral(", "));
    }

    if (p.interval == 1)
        return days == QLatin1String("every day") ? QStringLiteral("Every day") : QStringLiteral("Every %1").arg(days);
    if (p.interval == 2)
        return QStringLiteral("Every other week on %1").arg(days);
    return QStringLiteral("Every %1 weeks on %2").arg(p.interval).arg(days);
}

QVariantMap toVariant(const Task &t)
{
    QVariantMap m{
        {QStringLiteral("id"), t.id},
        {QStringLiteral("title"), t.title},
        {QStringLiteral("description"), t.description},
        {QStringLiteral("status"), nameOf(t.status)},
        {QStringLiteral("priority"), nameOf(t.priority)},
        {QStringLiteral("energy"), nameOf(t.energy)},
        {QStringLiteral("estimate"), t.estimateMinutes},
        {QStringLiteral("complexity"), t.complexity},
        {QStringLiteral("dueDate"), t.dueDate.isValid() ? t.dueDate.toString(Qt::ISODate) : QString()},
        {QStringLiteral("scheduledTime"), t.scheduledTime.isValid() ? t.scheduledTime.toString(QStringLiteral("HH:mm")) : QString()},
        {QStringLiteral("fixedTime"), t.fixedTime},
        {QStringLiteral("parentId"), t.parentId},
        {QStringLiteral("categoryId"), t.categoryId},
        {QStringLiteral("tags"), t.tags},
        {QStringLiteral("color"), t.color},
        {QStringLiteral("isRecurring"), t.isRecurring()},
        {QStringLiteral("createdAt"), t.createdAt},
    };
    if (t.recurrence) {
        m.insert(QStringLiteral("recurrence"), t.recurrence->toJson().toVariantMap());
        m.insert(QStringLiteral("recurrenceText"), describeRecurrence(*t.recurrence));
    }
    return m;
}

QVariantList scheduleToVariant(const CategorySchedule &s)
{
    QVariantList days;
    for (int d = 0; d < 7; ++d) {
        const auto w = s.windowForWeekday(d);
        days.append(QVariantMap{
            {QStringLiteral("day"), d},
            {QStringLiteral("name"), dayName(d, true)},
            {QStringLiteral("enabled"), w.has_value()},
            {QStringLiteral("start"), w ? w->start.toString(QStringLiteral("HH:mm")) : QStringLiteral("09:00")},
            {QStringLiteral("end"), w ? w->end.toString(QStringLiteral("HH:mm")) : QStringLiteral("17:00")},
        });
    }
    return days;
}

CategorySchedule scheduleFromVariant(const QVariantList &days)
{
    CategorySchedule s;
    for (const QVariant &v : days) {
        const QVariantMap d = v.toMap();
        const int day = d.value(QStringLiteral("day")).toInt();
        if (!d.value(QStringLiteral("enabled")).toBool()) {
            s.setWindow(day, std::nullopt);
            continue;
        }
        const QTime start = QTime::fromString(d.value(QStringLiteral("start")).toString(), QStringLiteral("HH:mm"));
        const QTime end = QTime::fromString(d.value(QStringLiteral("end")).toString(), QStringLiteral("HH:mm"));
        s.setWindow(day, start.isValid() && end.isValid() && start < end ? std::optional<TimeWindow>(TimeWindow{start, end})
                                                                         : std::nullopt);
    }
    return s;
}

QVariantMap toVariant(const Category &c)
{
    return {
        {QStringLiteral("id"), c.id},
        {QStringLiteral("name"), c.name},
        {QStringLiteral("color"), c.color},
        {QStringLiteral("isDefault"), c.isDefault},
        {QStringLiteral("showOnSchedule"), c.showOnSchedule},
        {QStringLiteral("days"), scheduleToVariant(c.schedule)},
    };
}

QVariantMap toVariant(const AgendaItem &item)
{
    return {
        {QStringLiteral("kind"), kindName(item.kind)},
        {QStringLiteral("state"), stateName(item.state)},
        {QStringLiteral("id"), item.id},
        {QStringLiteral("taskId"), item.taskId},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("start"), item.start},
        {QStringLiteral("end"), item.end},
        {QStringLiteral("allDay"), item.allDay},
        {QStringLiteral("timeless"), item.timeless},
        {QStringLiteral("working"), item.working},
        {QStringLiteral("startsSoon"), item.startsSoon},
        {QStringLiteral("bumpedCount"), item.bumpedCount},
        {QStringLiteral("color"), item.color},
        {QStringLiteral("priority"), nameOf(item.priority)},
        {QStringLiteral("energy"), nameOf(item.energy)},
    };
}

QVariantMap toVariant(const ScheduleBlock &s)
{
    return {
        {QStringLiteral("id"), s.id},
        {QStringLiteral("taskId"), s.taskId},
        {QStringLiteral("start"), s.start},
        {QStringLiteral("end"), s.end},
        {QStringLiteral("completed"), s.completed},
        {QStringLiteral("bumpedCount"), s.bumpedCount},
    };
}

QVariantMap toVariant(const WorkLog &w)
{
    return {
        {QStringLiteral("id"), w.id},
        {QStringLiteral("taskId"), w.taskId},
        {QStringLiteral("startedAt"), w.startedAt},
        {QStringLiteral("endedAt"), w.endedAt ? QVariant(*w.endedAt) : QVariant()},
        {QStringLiteral("minutes"), w.durationMinutes ? QVariant(*w.durationMinutes) : QVariant()},
    };
}

QVariantList toVariantList(const QList<AgendaItem> &items)
{
    QVariantList out;
    for (const AgendaItem &i : items)
        out.append(toVariant(i));
    return out;
}

} // namespace tumbler
