#include "calendarchanges.h"

#include <QJsonObject>

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;
} // namespace

QString nameOf(CalendarChange::Kind kind)
{
    switch (kind) {
    case CalendarChange::Kind::Added:
        return QStringLiteral("added");
    case CalendarChange::Kind::Moved:
        return QStringLiteral("moved");
    case CalendarChange::Kind::Cancelled:
        return QStringLiteral("cancelled");
    case CalendarChange::Kind::Changed:
        return QStringLiteral("changed");
    }
    return {};
}

std::optional<CalendarChange::Kind> changeKindFromString(const QString &s)
{
    for (auto k : {CalendarChange::Kind::Added, CalendarChange::Kind::Moved, CalendarChange::Kind::Cancelled, CalendarChange::Kind::Changed}) {
        if (nameOf(k) == s)
            return k;
    }
    return std::nullopt;
}

QJsonArray toJson(const QList<CalendarChange> &changes)
{
    QJsonArray out;
    for (const CalendarChange &c : changes) {
        QJsonObject o{
            {QStringLiteral("key"), c.key},
            {QStringLiteral("kind"), nameOf(c.kind)},
            {QStringLiteral("title"), c.title},
            {QStringLiteral("start"), double(c.start)},
            {QStringLiteral("end"), double(c.end)},
            {QStringLiteral("seenAt"), double(c.seenAt)},
        };
        if (!c.eventId.isEmpty())
            o.insert(QStringLiteral("eventId"), c.eventId);
        if (!c.location.isEmpty())
            o.insert(QStringLiteral("location"), c.location);
        if (!c.oldLocation.isEmpty())
            o.insert(QStringLiteral("oldLocation"), c.oldLocation);
        if (c.oldStart) {
            o.insert(QStringLiteral("oldStart"), double(c.oldStart));
            o.insert(QStringLiteral("oldEnd"), double(c.oldEnd));
        }
        out.append(o);
    }
    return out;
}

QList<CalendarChange> changesFromJson(const QJsonArray &array)
{
    QList<CalendarChange> out;
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        const auto kind = changeKindFromString(o.value(QLatin1String("kind")).toString());
        const QString key = o.value(QLatin1String("key")).toString();
        if (!kind || key.isEmpty())
            continue;
        CalendarChange c;
        c.key = key;
        c.kind = *kind;
        c.eventId = o.value(QLatin1String("eventId")).toString();
        c.title = o.value(QLatin1String("title")).toString();
        c.location = o.value(QLatin1String("location")).toString();
        c.oldLocation = o.value(QLatin1String("oldLocation")).toString();
        c.start = qint64(o.value(QLatin1String("start")).toDouble());
        c.end = qint64(o.value(QLatin1String("end")).toDouble());
        c.oldStart = qint64(o.value(QLatin1String("oldStart")).toDouble());
        c.oldEnd = qint64(o.value(QLatin1String("oldEnd")).toDouble());
        c.seenAt = qint64(o.value(QLatin1String("seenAt")).toDouble());
        out.append(c);
    }
    return out;
}

QList<CalendarChange> mergeChanges(QList<CalendarChange> kept, const QList<CalendarChange> &fresh, qint64 now)
{
    using Kind = CalendarChange::Kind;
    for (const CalendarChange &f : fresh) {
        const auto it = std::find_if(kept.begin(), kept.end(), [&f](const CalendarChange &k) { return k.key == f.key; });
        if (it == kept.end()) {
            kept.append(f);
            continue;
        }
        CalendarChange merged = f;
        if (f.kind != Kind::Cancelled) {
            if (it->kind == Kind::Added) {
                merged.kind = Kind::Added; // still new to the user, just at its latest time
                merged.oldStart = merged.oldEnd = 0;
                merged.oldLocation.clear();
            } else if (it->kind == Kind::Moved) {
                // Moved twice: from where it was first, to where it is now.
                merged.kind = Kind::Moved;
                merged.oldStart = it->oldStart;
                merged.oldEnd = it->oldEnd;
                if (merged.oldLocation.isEmpty())
                    merged.oldLocation = it->oldLocation;
            } else if (it->kind == Kind::Changed && f.kind == Kind::Changed && it->oldLocation == f.location) {
                kept.erase(it); // the room changed back
                continue;
            } else if (it->kind == Kind::Changed && merged.oldLocation.isEmpty()) {
                merged.oldLocation = it->oldLocation;
            }
        }
        if (merged.kind == Kind::Moved && merged.start == merged.oldStart && merged.end == merged.oldEnd) {
            // Moved back to where it was: what's left is a new room, if any.
            if (merged.oldLocation.isEmpty() || merged.oldLocation == merged.location) {
                kept.erase(it);
                continue;
            }
            merged.kind = Kind::Changed;
            merged.oldStart = merged.oldEnd = 0;
        }
        *it = merged;
    }
    kept.erase(std::remove_if(kept.begin(), kept.end(), [now](const CalendarChange &c) { return c.over() <= now; }), kept.end());
    std::sort(kept.begin(), kept.end(), [](const CalendarChange &a, const CalendarChange &b) { return a.affects() < b.affects(); });
    return kept;
}

ChangeUrgency urgencyOf(const CalendarChange &change, qint64 now)
{
    const qint64 lead = change.affects() - now;
    if (lead <= 30 * kMinute)
        return ChangeUrgency::Soon;
    if (lead <= 120 * kMinute)
        return ChangeUrgency::Today;
    return ChangeUrgency::Later;
}

QString nameOf(ChangeUrgency urgency)
{
    switch (urgency) {
    case ChangeUrgency::Later:
        return QStringLiteral("later");
    case ChangeUrgency::Today:
        return QStringLiteral("today");
    case ChangeUrgency::Soon:
        return QStringLiteral("soon");
    }
    return {};
}

} // namespace tumbler
