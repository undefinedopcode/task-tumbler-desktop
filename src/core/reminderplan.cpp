#include "reminderplan.h"

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;
}

QList<Reminder> dueReminders(const TodayView &view, qint64 now, qint64 leadMs, qint64 startedAt, QSet<QString> &fired)
{
    QList<Reminder> out;
    const auto once = [&fired, &out](Reminder::Kind kind, const AgendaItem &item, const QString &prefix) {
        const QString key = prefix + item.id + QLatin1Char('@') + QString::number(item.start);
        if (fired.contains(key))
            return;
        fired.insert(key);
        out.append({kind, item, key});
    };

    QList<AgendaItem> live = view.upcoming + view.now;
    for (const AgendaItem &item : std::as_const(live)) {
        if (item.timeless || item.allDay)
            continue;
        const qint64 untilStart = item.start - now;
        if (leadMs > 0 && untilStart > 0 && untilStart <= leadMs && untilStart > leadMs - kReminderGraceMs)
            once(Reminder::Kind::StartingSoon, item, QStringLiteral("soon:"));
        if (untilStart <= 0 && -untilStart <= kReminderGraceMs && !item.working)
            once(Reminder::Kind::StartingNow, item, QStringLiteral("now:"));
        if (item.kind == AgendaItem::Kind::Schedule && item.working && item.end > now && item.end - now <= 5 * kMinute)
            once(Reminder::Kind::WrapUp, item, QStringLiteral("wrap:"));
    }

    for (const AgendaItem &item : view.overdue) {
        if (item.kind != AgendaItem::Kind::Schedule || item.working)
            continue;
        if (now - item.end > kReminderGraceMs || item.end < startedAt)
            continue;
        once(Reminder::Kind::Missed, item, QStringLiteral("missed:"));
    }
    return out;
}

} // namespace tumbler
