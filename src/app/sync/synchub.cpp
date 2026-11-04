#include "synchub.h"

#include "appcontroller.h"
#include "desktop/devicecalendars.h"
#include "keystore.h"
#include "operations.h"

#include <QJsonArray>
#include <QJsonObject>

namespace tumbler {

namespace {

SyncHub *s_instance = nullptr;
const QString kLinearKey = QStringLiteral("linear-api-key");
constexpr qint64 kDay = 24 * 3600 * 1000LL;
constexpr int kEveryMs = 15 * 60 * 1000;
constexpr int kFeedsEveryMs = 5 * 60 * 1000;

QString plural(int n, const QString &one, const QString &many)
{
    return QString::number(n) + QLatin1Char(' ') + (n == 1 ? one : many);
}

} // namespace

SyncHub::SyncHub(AppController *app, KeyStore *keys, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_keys(keys)
    , m_linear(&m_network, this)
    , m_fetcher(&m_network, this)
{
    m_every.setInterval(kEveryMs);
    connect(&m_every, &QTimer::timeout, this, &SyncHub::syncNow);
    m_feedsEvery.setInterval(kFeedsEveryMs);
    connect(&m_feedsEvery, &QTimer::timeout, this, [this] {
        if (m_running == 0)
            syncFeeds([] {});
    });

    // Finishing a linked task: tell the service shortly after.
    m_push.setSingleShot(true);
    m_push.setInterval(3000);
    connect(&m_push, &QTimer::timeout, this, [this] { withLinearKey([this] { pushCompletions(); }); });
    connect(app, &AppController::changed, this, [this](const QStringList &tables) {
        if (tables.contains(QLatin1String("tasks")) && linearSettings().value(QLatin1String("enabled")).toBool())
            m_push.start();
    });
    connect(app, &AppController::settingsChanged, this, &SyncHub::stateChanged);
    m_deviceChanged.setSingleShot(true);
    m_deviceChanged.setInterval(2000);
    connect(&m_deviceChanged, &QTimer::timeout, this, &SyncHub::syncDeviceFeeds);
}

void SyncHub::setInstance(SyncHub *hub)
{
    s_instance = hub;
}

void SyncHub::setAutomatic(bool on)
{
    if (!on) {
        m_every.stop();
        m_feedsEvery.stop();
        return;
    }
    m_every.start();
    m_feedsEvery.start();
    watchDeviceCalendars();
    // Shortly after start, once the window is up and the network likely is.
    QTimer::singleShot(20000, this, &SyncHub::syncNow);
}

// ---- settings --------------------------------------------------------------------

QJsonObject SyncHub::linearSettings() const
{
    return m_app->queries().setting(QStringLiteral("linear"), QJsonObject()).toObject();
}

void SyncHub::saveLinearSettings(const QJsonObject &s)
{
    m_app->setSetting(QStringLiteral("linear"), s.toVariantMap());
}

QList<sync::FeedSettings> SyncHub::feeds() const
{
    QList<sync::FeedSettings> out;
    for (const QJsonValue &v : m_app->queries().setting(QStringLiteral("calendar_feeds"), QJsonArray()).toArray())
        out.append(sync::FeedSettings::fromJson(v.toObject()));
    return out;
}

void SyncHub::saveFeeds(const QList<sync::FeedSettings> &list)
{
    QVariantList out;
    for (const sync::FeedSettings &f : list)
        out.append(f.toJson().toVariantMap());
    m_app->setSetting(QStringLiteral("calendar_feeds"), out);
}

sync::LinearFilter SyncHub::linearFilter() const
{
    const QJsonObject s = linearSettings();
    sync::LinearFilter f;
    for (const QJsonValue &k : s.value(QLatin1String("teamKeys")).toArray())
        f.teamKeys.append(k.toString());
    if (s.value(QLatin1String("includeBacklog")).toBool())
        f.stateTypes.prepend(QStringLiteral("backlog"));
    return f;
}

QString SyncHub::integration(const QString &provider)
{
    return sync::ensureIntegration(m_app->queries().db(), provider, m_app->now()).value_or(QString());
}

QVariantMap SyncHub::state() const
{
    const QJsonObject s = linearSettings();
    QVariantList feedList;
    for (const sync::FeedSettings &f : feeds()) {
        const FeedStatus st = m_feedStatus.value(f.id);
        QVariantMap m = f.toJson().toVariantMap();
        m.insert(QStringLiteral("error"), st.error);
        m.insert(QStringLiteral("lastSync"), st.lastSync);
        m.insert(QStringLiteral("count"), st.count);
        m.insert(QStringLiteral("declaredZone"), st.declaredZone);
        m.insert(QStringLiteral("device"), devicecalendars::calendarOf(f.url).has_value());
        feedList.append(m);
    }
    QVariantMap linear = s.toVariantMap();
    linear.insert(QStringLiteral("enabled"), s.value(QLatin1String("enabled")).toBool());
    linear.insert(QStringLiteral("error"), m_linearError);
    linear.insert(QStringLiteral("lastSync"), m_linearLast);
    linear.insert(QStringLiteral("summary"), m_linearSummary);
    return {
        {QStringLiteral("syncing"), m_running > 0},
        {QStringLiteral("linear"), linear},
        {QStringLiteral("feeds"), feedList},
    };
}

// ---- Linear ----------------------------------------------------------------------

void SyncHub::withLinearKey(std::function<void()> then)
{
    if (!linearSettings().value(QLatin1String("enabled")).toBool())
        return;
    if (m_keyLoaded) {
        then();
        return;
    }
    m_keys->read(kLinearKey, [this, then](const QString &value, const QString &error) {
        if (value.isEmpty()) {
            m_linearError = error.isEmpty() ? QStringLiteral("the Linear API key is missing; connect again") : error;
            Q_EMIT stateChanged();
            return;
        }
        m_linear.setApiKey(value);
        m_keyLoaded = true;
        then();
    });
}

void SyncHub::connectLinear(const QString &apiKey)
{
    const QString key = apiKey.trimmed();
    if (key.isEmpty())
        return;
    m_linear.setApiKey(key);
    m_linear.viewer([this, key](const QString &name, const QVariantList &teams, const QString &error, bool) {
        if (!error.isEmpty()) {
            m_linearError = error;
            m_keyLoaded = false;
            Q_EMIT m_app->message(error);
            Q_EMIT stateChanged();
            return;
        }
        m_keys->write(kLinearKey, key, [this, name, teams](const QString &writeError) {
            if (!writeError.isEmpty()) {
                Q_EMIT m_app->message(QStringLiteral("Couldn't store the Linear key: %1").arg(writeError));
                return;
            }
            m_keyLoaded = true;
            m_linearError.clear();
            QJsonObject s = linearSettings();
            s.insert(QStringLiteral("enabled"), true);
            s.insert(QStringLiteral("user"), name);
            s.insert(QStringLiteral("teams"), QJsonArray::fromVariantList(teams));
            saveLinearSettings(s);
            Q_EMIT m_app->message(QStringLiteral("Connected to Linear as %1").arg(name));
            syncNow();
        });
    });
}

void SyncHub::disconnectLinear()
{
    m_keys->write(kLinearKey, QString(), [](const QString &) {});
    m_keyLoaded = false;
    m_linear.setApiKey({});
    m_linearError.clear();
    QJsonObject s = linearSettings();
    s.insert(QStringLiteral("enabled"), false);
    saveLinearSettings(s);
}

void SyncHub::setLinearTeams(const QStringList &keys)
{
    QJsonObject s = linearSettings();
    s.insert(QStringLiteral("teamKeys"), QJsonArray::fromStringList(keys));
    saveLinearSettings(s);
}

void SyncHub::setLinearBacklog(bool include)
{
    QJsonObject s = linearSettings();
    s.insert(QStringLiteral("includeBacklog"), include);
    saveLinearSettings(s);
}

void SyncHub::syncLinear(std::function<void()> done)
{
    if (!linearSettings().value(QLatin1String("enabled")).toBool()) {
        done();
        return;
    }
    // A key that can't be read leaves `done` uncalled; make sure it runs.
    auto finished = std::make_shared<bool>(false);
    auto finish = [finished, done] {
        if (!*finished) {
            *finished = true;
            done();
        }
    };
    if (!m_keyLoaded) {
        QTimer::singleShot(5000, this, finish); // the keyring prompt can take a while; don't hold the rest up forever
    }
    withLinearKey([this, finish] {
        const sync::LinearFilter filter = linearFilter();
        m_linear.activeIssues(filter, [this, filter, finish](const QList<sync::RemoteTask> &issues, const QString &error, bool auth) {
            if (!error.isEmpty()) {
                m_linearError = auth ? QStringLiteral("Linear didn't accept the API key; connect again") : error;
                finish();
                return;
            }
            const QString id = integration(QStringLiteral("linear"));
            const QStringList gone = sync::missing(m_app->queries(), id, issues);
            m_linear.whatBecameOf(gone, filter, [this, id, issues, finish](const QHash<QString, sync::Gone> &what,
                                                                           const QHash<QString, sync::Worked> &worked, const QString &) {
                sync::PullResult r;
                QString error;
                const Queries q = m_app->queries();
                auto m = sync::pullTasks(q, id, issues, what, m_app->now(), &r, &error);
                // Closed there without the timer here: the time still counts.
                int logged = 0;
                if (m) {
                    QList<Mutation> parts{*m};
                    for (auto it = r.completedThere.cbegin(); it != r.completedThere.cend(); ++it) {
                        int minutes = 0;
                        if (const auto log = sync::logWorkedThere(q, it.key(), worked.value(it.value()), QStringLiteral("Linear"),
                                                                  m_app->zone(), m_app->now(), &minutes)) {
                            parts.append(*log);
                            logged += minutes;
                        }
                    }
                    m = combine(m->text, parts);
                }
                if (!m || !m_app->applyExternal(*m, &error)) {
                    m_linearError = QStringLiteral("couldn't save what came from Linear: %1").arg(error);
                    finish();
                    return;
                }
                m_linearError.clear();
                m_linearLast = m_app->now();
                QStringList parts;
                if (r.created)
                    parts.append(plural(r.created, QStringLiteral("new task"), QStringLiteral("new tasks")));
                if (r.updated)
                    parts.append(plural(r.updated, QStringLiteral("update"), QStringLiteral("updates")));
                if (r.closed)
                    parts.append(plural(r.closed, QStringLiteral("closed"), QStringLiteral("closed")));
                if (logged)
                    parts.append(QStringLiteral("%1 min logged").arg(logged));
                m_linearSummary = parts.isEmpty() ? QStringLiteral("Up to date") : parts.join(QStringLiteral(", "));
                if (r.created || r.closed)
                    Q_EMIT m_app->message(QStringLiteral("Linear: %1").arg(m_linearSummary));
                finish();
                pushCompletions();
            });
        });
    });
}

void SyncHub::pushCompletions()
{
    if (m_pushing || !m_keyLoaded)
        return;
    const QString id = integration(QStringLiteral("linear"));
    const QList<sync::PendingPush> pending = sync::pendingCompletions(m_app->queries(), id);
    if (pending.isEmpty())
        return;
    m_pushing = true;
    pushNext(id, pending);
}

// One at a time, so a failure doesn't stop the rest and Linear isn't flooded.
void SyncHub::pushNext(const QString &integrationId, QList<sync::PendingPush> pending)
{
    if (pending.isEmpty()) {
        m_pushing = false;
        Q_EMIT stateChanged();
        return;
    }
    const sync::PendingPush p = pending.takeFirst();
    m_linear.complete(p.externalId, [this, integrationId, p, pending](const QString &error) {
        if (error.isEmpty())
            m_app->applyExternal(sync::markPushed(integrationId, p.externalId));
        else
            m_linearError = QStringLiteral("couldn't mark “%1” done in Linear: %2").arg(p.title, error);
        pushNext(integrationId, pending);
    });
}

// ---- calendar feeds ------------------------------------------------------------------

void SyncHub::syncFeed(const sync::FeedSettings &feed, const QByteArray &data, QString *error)
{
    const qint64 now = m_app->now();
    const QDateTime from = QDateTime::fromMSecsSinceEpoch(now - kDay, m_app->zone());
    const QDateTime to = QDateTime::fromMSecsSinceEpoch(now + 60 * kDay, m_app->zone());
    QList<sync::RemoteEvent> events;
    if (const auto calendar = devicecalendars::calendarOf(feed.url))
        events = devicecalendars::toRemoteEvents(devicecalendars::occurrences(*calendar, from, to, error), feed);
    else {
        events = sync::parseIcs(data, feed, from, to, error);
        m_feedStatus[feed.id].declaredZone = sync::declaredZone(data);
    }
    applyFeed(feed, events, from.toMSecsSinceEpoch(), to.toMSecsSinceEpoch(), *error);
    *error = m_feedStatus.value(feed.id).error;
}

void SyncHub::applyFeed(const sync::FeedSettings &feed, const QList<sync::RemoteEvent> &events, qint64 from, qint64 to,
                        const QString &parseError)
{
    const qint64 now = m_app->now();
    QString error = parseError;
    if (error.isEmpty()) {
        sync::FeedResult r;
        const QList<sync::RemoteEvent> placed = feed.dayZone.isEmpty()
            ? events
            : sync::moveAllDay(events, QTimeZone(feed.dayZone.toUtf8()), m_app->zone());
        const auto m = sync::pullFeed(m_app->queries(), integration(QStringLiteral("ics")), feed.id, placed, from, to,
                                      QString::fromUtf8(m_app->zone().id()), now, &r, &error);
        if (m && m_app->applyExternal(*m, &error) && !r.changes.isEmpty())
            m_app->noteCalendarChanges(r.changes);
    }
    FeedStatus &st = m_feedStatus[feed.id];
    st.error = error;
    if (error.isEmpty()) {
        st.lastSync = now;
        st.count = int(events.size());
    }
}

void SyncHub::refreshFeed(const sync::FeedSettings &feed, std::function<void()> done)
{
    if (devicecalendars::calendarOf(feed.url)) {
        QString error;
        syncFeed(feed, {}, &error);
        done();
        return;
    }
    m_fetcher.fetch(feed.url, [this, feed, done](const QByteArray &data, const QString &fetchError) {
        QString error = fetchError;
        if (error.isEmpty())
            syncFeed(feed, data, &error);
        else
            m_feedStatus[feed.id].error = error;
        done();
    });
}

void SyncHub::syncFeeds(std::function<void()> done)
{
    QList<sync::FeedSettings> todo;
    for (const sync::FeedSettings &f : feeds()) {
        if (f.enabled)
            todo.append(f);
    }
    if (todo.isEmpty()) {
        done();
        return;
    }
    auto left = std::make_shared<int>(int(todo.size()));
    for (const sync::FeedSettings &f : todo) {
        refreshFeed(f, [left, done] {
            if (--*left == 0)
                done();
        });
    }
}

void SyncHub::syncDeviceFeeds()
{
    for (const sync::FeedSettings &f : feeds()) {
        if (f.enabled && devicecalendars::calendarOf(f.url))
            refreshFeed(f, [] {});
    }
    Q_EMIT stateChanged();
}

void SyncHub::watchDeviceCalendars()
{
    if (m_watchingDevice || !devicecalendars::supported() || devicecalendars::access() != devicecalendars::Access::Granted)
        return;
    m_watchingDevice = true;
    devicecalendars::watch(this, [this] { m_deviceChanged.start(); });
}

QVariantMap SyncHub::deviceCalendars() const
{
    using devicecalendars::Access;
    const Access access = devicecalendars::supported() ? devicecalendars::access() : Access::Denied;
    QHash<QString, QString> added; // calendar id -> feed id
    for (const sync::FeedSettings &f : feeds()) {
        if (const auto calendar = devicecalendars::calendarOf(f.url))
            added.insert(*calendar, f.id);
    }
    QVariantList list;
    for (const devicecalendars::Calendar &c : devicecalendars::calendars()) {
        list.append(QVariantMap{{QStringLiteral("id"), c.id},
                                {QStringLiteral("title"), c.title},
                                {QStringLiteral("account"), c.account},
                                {QStringLiteral("color"), c.color},
                                {QStringLiteral("feedId"), added.value(c.id)}});
    }
    return {
        {QStringLiteral("supported"), devicecalendars::supported()},
        {QStringLiteral("access"), access == Access::Granted ? QStringLiteral("granted")
                                   : access == Access::NotAsked ? QStringLiteral("notAsked")
                                                                : QStringLiteral("denied")},
        {QStringLiteral("calendars"), list},
    };
}

void SyncHub::requestCalendarAccess()
{
    devicecalendars::requestAccess(this, [this](bool granted) {
        if (granted)
            watchDeviceCalendars();
        Q_EMIT deviceCalendarsChanged();
    });
}

void SyncHub::addDeviceCalendar(const QString &calendarId)
{
    const QList<devicecalendars::Calendar> all = devicecalendars::calendars();
    const auto it = std::find_if(all.cbegin(), all.cend(), [&](const devicecalendars::Calendar &c) { return c.id == calendarId; });
    if (it == all.cend()) {
        Q_EMIT feedFailed(QStringLiteral("that calendar isn't on this computer"));
        return;
    }
    for (const sync::FeedSettings &f : feeds()) {
        if (f.url == devicecalendars::feedUrl(calendarId))
            return; // already there
    }
    sync::FeedSettings f;
    f.id = newId();
    f.name = it->title;
    f.url = devicecalendars::feedUrl(calendarId);
    f.color = it->color;
    QList<sync::FeedSettings> list = feeds();
    list.append(f);
    saveFeeds(list);
    QString error;
    syncFeed(f, {}, &error);
    Q_EMIT stateChanged();
    Q_EMIT deviceCalendarsChanged();
    Q_EMIT feedAdded(f.id);
    Q_EMIT m_app->message(QStringLiteral("Showing “%1” on your schedule").arg(f.name));
}

void SyncHub::addFeed(const QVariantMap &v)
{
    sync::FeedSettings f = sync::FeedSettings::fromJson(QJsonObject::fromVariantMap(v));
    f.id = newId();
    f.enabled = true;
    if (f.url.trimmed().isEmpty()) {
        Q_EMIT feedFailed(QStringLiteral("enter the calendar's address"));
        return;
    }
    if (f.name.trimmed().isEmpty())
        f.name = sync::FeedFetcher::normalise(f.url).host().isEmpty() ? QStringLiteral("Calendar") : sync::FeedFetcher::normalise(f.url).host();
    m_fetcher.fetch(f.url, [this, f](const QByteArray &data, const QString &fetchError) {
        if (!fetchError.isEmpty()) {
            Q_EMIT feedFailed(fetchError);
            return;
        }
        QString error;
        sync::parseIcs(data, f, QDateTime::currentDateTime(), QDateTime::currentDateTime().addDays(1), &error);
        if (!error.isEmpty()) {
            Q_EMIT feedFailed(error);
            return;
        }
        QList<sync::FeedSettings> list = feeds();
        list.append(f);
        saveFeeds(list);
        syncFeed(f, data, &error);
        Q_EMIT stateChanged();
        Q_EMIT feedAdded(f.id);
        Q_EMIT m_app->message(QStringLiteral("Subscribed to “%1”").arg(f.name));
    });
}

void SyncHub::updateFeed(const QString &id, const QVariantMap &changes)
{
    QList<sync::FeedSettings> list = feeds();
    for (sync::FeedSettings &f : list) {
        if (f.id != id)
            continue;
        QJsonObject o = f.toJson();
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            if (it.key() != QLatin1String("id"))
                o.insert(it.key(), QJsonValue::fromVariant(it.value()));
        }
        f = sync::FeedSettings::fromJson(o);
        saveFeeds(list);
        if (!f.enabled) {
            // Off: its events leave the schedule until it's turned back on.
            m_app->applyExternal(sync::dropFeed(integration(QStringLiteral("ics")), id));
            m_feedStatus.remove(id);
            Q_EMIT stateChanged();
            return;
        }
        refreshFeed(f, [this] { Q_EMIT stateChanged(); });
        return;
    }
}

void SyncHub::removeFeed(const QString &id)
{
    QList<sync::FeedSettings> list = feeds();
    QString name;
    list.removeIf([&](const sync::FeedSettings &f) {
        if (f.id == id)
            name = f.name;
        return f.id == id;
    });
    m_app->applyExternal(sync::dropFeed(integration(QStringLiteral("ics")), id));
    m_feedStatus.remove(id);
    saveFeeds(list);
    Q_EMIT deviceCalendarsChanged();
    if (!name.isEmpty())
        Q_EMIT m_app->message(QStringLiteral("Unsubscribed from “%1”").arg(name));
}

// ---- both ------------------------------------------------------------------------

void SyncHub::syncNow()
{
    if (m_running > 0)
        return;
    m_running = 2;
    Q_EMIT stateChanged();
    syncLinear([this] { finishOne(); });
    syncFeeds([this] { finishOne(); });
}

void SyncHub::finishOne()
{
    if (--m_running > 0)
        return;
    m_running = 0;
    Q_EMIT stateChanged();
    Q_EMIT syncFinished();
}

} // namespace tumbler
