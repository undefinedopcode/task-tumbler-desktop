#include "devicecalendars.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <algorithm>

#import <AppKit/AppKit.h>
#import <EventKit/EventKit.h>

// EventKit, with ARC. One store for the app's lifetime: EventKit wants it
// kept, and its change notifications come from it.
namespace tumbler::devicecalendars {

namespace {

EKEventStore *store()
{
    static EKEventStore *s = [[EKEventStore alloc] init];
    return s;
}

QString str(NSString *s)
{
    return s ? QString::fromNSString(s) : QString();
}

QString hex(CGColorRef cg)
{
    NSColor *color = cg ? [[NSColor colorWithCGColor:cg] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] : nil;
    if (!color)
        return QStringLiteral("#64748b");
    const auto byte = [](CGFloat v) { return qBound(0, int(v * 255.0 + 0.5), 255); };
    return QStringLiteral("#%1%2%3")
        .arg(byte(color.redComponent), 2, 16, QLatin1Char('0'))
        .arg(byte(color.greenComponent), 2, 16, QLatin1Char('0'))
        .arg(byte(color.blueComponent), 2, 16, QLatin1Char('0'));
}

qint64 ms(NSDate *d)
{
    return qint64(d.timeIntervalSince1970 * 1000.0);
}

QDate localDate(NSDate *d)
{
    return QDateTime::fromMSecsSinceEpoch(ms(d)).date();
}

EKCalendar *calendarWithId(const QString &id)
{
    return [store() calendarWithIdentifier:id.toNSString()];
}

} // namespace

bool supported()
{
    return true;
}

Access access()
{
    switch ([EKEventStore authorizationStatusForEntityType:EKEntityTypeEvent]) {
    case EKAuthorizationStatusNotDetermined:
        return Access::NotAsked;
    case EKAuthorizationStatusFullAccess:
        return Access::Granted;
    default:
        // Denied, restricted, or "add events only", which can't read.
        return Access::Denied;
    }
}

void requestAccess(QObject *context, std::function<void(bool)> done)
{
    QPointer<QObject> guard(context);
    [store() requestFullAccessToEventsWithCompletion:^(BOOL granted, NSError *) {
        // EventKit answers on a queue of its own.
        const bool ok = granted;
        if (!guard)
            return;
        QMetaObject::invokeMethod(
            guard.data(),
            [done, ok] {
                [store() reset]; // see the calendars just allowed
                done(ok);
            },
            Qt::QueuedConnection);
    }];
}

QList<Calendar> calendars()
{
    QList<Calendar> out;
    if (access() != Access::Granted)
        return out;
    for (EKCalendar *c in [store() calendarsForEntityType:EKEntityTypeEvent])
        out.append({str(c.calendarIdentifier), str(c.title), str(c.source.title), hex(c.CGColor)});
    std::sort(out.begin(), out.end(), [](const Calendar &a, const Calendar &b) {
        return a.account != b.account ? a.account.localeAwareCompare(b.account) < 0 : a.title.localeAwareCompare(b.title) < 0;
    });
    return out;
}

QList<Occurrence> occurrences(const QString &calendarId, const QDateTime &from, const QDateTime &to, QString *error)
{
    QList<Occurrence> out;
    if (access() != Access::Granted) {
        if (error)
            *error = QStringLiteral("Task Tumbler isn't allowed to read your calendars (System Settings → Privacy & Security → Calendars)");
        return out;
    }
    EKCalendar *calendar = calendarWithId(calendarId);
    if (!calendar) {
        if (error)
            *error = QStringLiteral("that calendar is no longer on this Mac");
        return out;
    }
    NSPredicate *range = [store() predicateForEventsWithStartDate:from.toNSDate() endDate:to.toNSDate() calendars:@[ calendar ]];
    for (EKEvent *e in [store() eventsMatchingPredicate:range]) {
        if (e.status == EKEventStatusCanceled)
            continue;
        Occurrence o;
        // The item id is shared by a repeat's occurrences; its original
        // start tells them apart and survives one being moved.
        NSDate *instance = e.occurrenceDate ?: e.startDate;
        o.id = str(e.calendarItemIdentifier) + QLatin1Char('@') + QString::number(ms(instance));
        o.title = str(e.title);
        o.location = str(e.location);
        o.notes = str(e.notes);
        o.allDay = e.allDay;
        if (o.allDay) {
            // EventKit ends an all-day event at the end of its last day.
            o.startDate = localDate(e.startDate);
            o.endDate = localDate([e.endDate dateByAddingTimeInterval:-1]);
        } else {
            o.start = ms(e.startDate);
            o.end = ms(e.endDate);
        }
        o.free = e.availability == EKEventAvailabilityFree;
        out.append(o);
    }
    return out;
}

void watch(QObject *context, std::function<void()> changed)
{
    QPointer<QObject> guard(context);
    [[NSNotificationCenter defaultCenter] addObserverForName:EKEventStoreChangedNotification
                                                      object:store()
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification *) {
                                                      if (guard)
                                                          QMetaObject::invokeMethod(guard.data(), changed, Qt::QueuedConnection);
                                                  }];
}

} // namespace tumbler::devicecalendars
