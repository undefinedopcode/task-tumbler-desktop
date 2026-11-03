#include "icsfeed.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/OccurrenceIterator>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace tumbler::sync {

namespace {
constexpr qint64 kMaxBytes = 20 * 1024 * 1024;
constexpr int kTimeoutMs = 60000;
} // namespace

QJsonObject FeedSettings::toJson() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("url"), url},
        {QStringLiteral("color"), color},
        {QStringLiteral("blocking"), nameOf(blocking)},
        {QStringLiteral("allowedTags"), QJsonArray::fromStringList(allowedTags)},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("dayZone"), dayZone},
    };
}

FeedSettings FeedSettings::fromJson(const QJsonObject &o)
{
    FeedSettings f;
    f.id = o.value(QLatin1String("id")).toString();
    f.name = o.value(QLatin1String("name")).toString();
    f.url = o.value(QLatin1String("url")).toString();
    f.color = o.value(QLatin1String("color")).toString(f.color);
    f.blocking = blockingModeFromString(o.value(QLatin1String("blocking")).toString()).value_or(BlockingMode::BlockAll);
    for (const QJsonValue &t : o.value(QLatin1String("allowedTags")).toArray())
        f.allowedTags.append(t.toString());
    f.enabled = o.value(QLatin1String("enabled")).toBool(true);
    f.dayZone = o.value(QLatin1String("dayZone")).toString();
    return f;
}

QList<RemoteEvent> parseIcs(const QByteArray &ics, const FeedSettings &feed, const QDateTime &from, const QDateTime &to, QString *error)
{
    using namespace KCalendarCore;
    MemoryCalendar::Ptr calendar(new MemoryCalendar(QTimeZone::systemTimeZone()));
    ICalFormat format;
    if (!ics.contains("BEGIN:VCALENDAR") || !format.fromRawString(calendar, ics)) {
        if (error)
            *error = QStringLiteral("that isn't a calendar (.ics) file");
        return {};
    }

    QList<RemoteEvent> out;
    OccurrenceIterator it(*calendar, from, to);
    while (it.hasNext()) {
        it.next();
        const auto event = it.incidence().dynamicCast<KCalendarCore::Event>();
        if (!event || event->status() == Incidence::StatusCanceled)
            continue;
        const QDateTime start = it.occurrenceStartDate();
        QDateTime end = it.occurrenceEndDate();
        if (!end.isValid() || end < start)
            end = start;

        RemoteEvent r;
        const bool repeats = event->recurs() || event->hasRecurrenceId();
        const QDateTime instance = it.recurrenceId().isValid() ? it.recurrenceId() : start;
        r.externalId = repeats ? event->uid() + QLatin1Char('@')
                + (event->allDay() ? instance.date().toString(Qt::ISODate) : QString::number(instance.toMSecsSinceEpoch()))
                               : event->uid();
        // CLASS:PRIVATE is ignored: a feed you subscribe to is your own view
        // (Google marks events private on its secret address). Busy-only
        // feeds leave the summary out, and those show as "(busy)".
        r.event.title = event->summary().trimmed().isEmpty() ? QStringLiteral("(busy)") : event->summary();
        r.event.description = event->description().left(4000);
        r.event.location = event->location();
        r.event.color = feed.color;
        r.event.allDay = event->allDay();
        if (r.event.allDay) {
            r.event.startDate = start.date();
            // KCalendarCore's all-day end is the last day, inclusive, like ours.
            r.event.endDate = std::max(start.date(), end.date());
        } else {
            r.event.startAt = start.toMSecsSinceEpoch();
            r.event.endAt = std::max(end.toMSecsSinceEpoch(), r.event.startAt);
        }
        // "Show as free" events are information, not commitments. So are
        // all-day ones (holidays, birthdays): busy is the default in iCalendar,
        // and taking the whole day left nothing to plan.
        r.event.blocking.mode = r.event.allDay || event->transparency() == KCalendarCore::Event::Transparent ? BlockingMode::None
                                                                                                          : feed.blocking;
        r.event.blocking.allowedTags = feed.allowedTags;
        out.append(r);
    }
    std::sort(out.begin(), out.end(), [](const RemoteEvent &a, const RemoteEvent &b) { return a.externalId < b.externalId; });
    return out;
}

QString declaredZone(const QByteArray &ics)
{
    // A property line, possibly with parameters: X-WR-TIMEZONE;VALUE=TEXT:Europe/Paris
    static const QRegularExpression re(QStringLiteral("^X-WR-TIMEZONE[^:\\r\\n]*:([^\\r\\n]+)"), QRegularExpression::MultilineOption);
    const auto m = re.match(QString::fromUtf8(ics.left(64 * 1024)));
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

FeedFetcher::FeedFetcher(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

QUrl FeedFetcher::normalise(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.startsWith(QLatin1Char('/')) || trimmed.startsWith(QLatin1Char('~')))
        return QUrl::fromLocalFile(trimmed.startsWith(QLatin1Char('~')) ? QDir::homePath() + trimmed.mid(1) : trimmed);
    QUrl u(trimmed);
    if (u.scheme() == QLatin1String("webcal") || u.scheme() == QLatin1String("webcals"))
        u.setScheme(QStringLiteral("https"));
    return u;
}

void FeedFetcher::fetch(const QString &url, std::function<void(const QByteArray &, const QString &)> done)
{
    const QUrl u = normalise(url);
    if (u.isLocalFile()) {
        QFile f(u.toLocalFile());
        if (!f.open(QIODevice::ReadOnly)) {
            done({}, QStringLiteral("can't open %1").arg(u.toLocalFile()));
            return;
        }
        done(f.read(kMaxBytes), {});
        return;
    }
    if (u.scheme() != QLatin1String("https") && u.scheme() != QLatin1String("http")) {
        done({}, QStringLiteral("use an https://, webcal:// or file address"));
        return;
    }
    QNetworkRequest req(u);
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > kMaxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, status == 401 || status == 403 || status == 404
                         ? QStringLiteral("the calendar address was refused (HTTP %1); it may have changed").arg(status)
                         : QStringLiteral("couldn't fetch the calendar: %1").arg(reply->errorString()));
            return;
        }
        done(reply->readAll(), {});
    });
}

} // namespace tumbler::sync
