#include "devicecalendars.h"

#include <algorithm>

namespace tumbler::devicecalendars {

namespace {
const QString kScheme = QStringLiteral("device:");
}

QString feedUrl(const QString &calendarId)
{
    return kScheme + calendarId;
}

std::optional<QString> calendarOf(const QString &url)
{
    if (!url.startsWith(kScheme) || url.size() == kScheme.size())
        return std::nullopt;
    return url.mid(kScheme.size());
}

QList<sync::RemoteEvent> toRemoteEvents(const QList<Occurrence> &occurrences, const sync::FeedSettings &feed)
{
    QList<sync::RemoteEvent> out;
    for (const Occurrence &o : occurrences) {
        sync::RemoteEvent r;
        r.externalId = o.id;
        r.event.title = o.title.trimmed().isEmpty() ? QStringLiteral("(busy)") : o.title;
        r.event.description = o.notes.left(4000);
        r.event.location = o.location;
        r.event.color = feed.color;
        r.event.allDay = o.allDay;
        if (o.allDay) {
            r.event.startDate = o.startDate;
            r.event.endDate = std::max(o.startDate, o.endDate);
        } else {
            r.event.startAt = o.start;
            r.event.endAt = std::max(o.end, o.start);
        }
        // "Show as free" events are information, not commitments.
        r.event.blocking.mode = o.free ? BlockingMode::None : feed.blocking;
        r.event.blocking.allowedTags = feed.allowedTags;
        out.append(r);
    }
    std::sort(out.begin(), out.end(), [](const sync::RemoteEvent &a, const sync::RemoteEvent &b) { return a.externalId < b.externalId; });
    return out;
}

} // namespace tumbler::devicecalendars
