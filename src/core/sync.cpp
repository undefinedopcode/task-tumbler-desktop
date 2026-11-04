#include "sync.h"

#include "operations.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>

namespace tumbler::sync {

namespace {

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

bool exec(QSqlDatabase db, const QString &sql, const QVariantList &binds, QString *error)
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec()) {
        setError(error, q.lastError().text());
        return false;
    }
    return true;
}

Mutation raw(const QList<Scope> &scopes, const QString &sql, const QVariantList &binds)
{
    Mutation m;
    m.scopes = scopes;
    m.apply = [sql, binds](QSqlDatabase db, QString *error) { return exec(db, sql, binds, error); };
    return m;
}

QString sha1(const QStringList &parts)
{
    return QString::fromLatin1(QCryptographicHash::hash(parts.join(QChar(0x1f)).toUtf8(), QCryptographicHash::Sha1).toHex());
}

struct Link {
    QString id;
    QString externalId;
    QString taskId; // empty: the task was deleted here
    QString hash;
    QString status;
};

QHash<QString, Link> links(const Queries &q, const QString &integrationId)
{
    QHash<QString, Link> out;
    QSqlQuery s(q.db());
    s.prepare(QStringLiteral("SELECT id, external_id, task_id, data_hash, sync_status FROM synced_items WHERE integration_id = ?"));
    s.addBindValue(integrationId);
    s.exec();
    while (s.next()) {
        Link l{s.value(0).toString(), s.value(1).toString(), s.value(2).toString(), s.value(3).toString(), s.value(4).toString()};
        out.insert(l.externalId, l);
    }
    return out;
}

QString historyJson(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

QString RemoteTask::hash() const
{
    return sha1({title, description, nameOf(priority), dueDate.toString(Qt::ISODate), tags.join(QLatin1Char(',')), url,
                 open ? QStringLiteral("open") : QStringLiteral("closed")});
}

std::optional<QString> ensureIntegration(QSqlDatabase db, const QString &provider, qint64 now, QString *error)
{
    QSqlQuery s(db);
    s.prepare(QStringLiteral("SELECT id FROM integrations WHERE provider = ?"));
    s.addBindValue(provider);
    if (s.exec() && s.next())
        return s.value(0).toString();
    const QString id = newId();
    if (!exec(db, QStringLiteral("INSERT INTO integrations (id, provider, created_at, updated_at) VALUES (?, ?, ?, ?)"),
              {id, provider, now, now}, error))
        return std::nullopt;
    return id;
}

QStringList missing(const Queries &q, const QString &integrationId, const QList<RemoteTask> &active)
{
    QSet<QString> seen;
    for (const RemoteTask &r : active)
        seen.insert(r.externalId);
    QStringList out;
    for (const Link &l : links(q, integrationId)) {
        if (!seen.contains(l.externalId))
            out.append(l.externalId);
    }
    out.sort();
    return out;
}

std::optional<Mutation> pullTasks(const Queries &q, const QString &integrationId, const QList<RemoteTask> &active,
                                  const QHash<QString, Gone> &gone, qint64 now, PullResult *result, QString *error)
{
    PullResult r;
    QList<Mutation> parts;
    const QHash<QString, Link> existing = links(q, integrationId);
    const auto category = q.defaultCategory();
    const Scope linkScope{QStringLiteral("synced_items"), QStringLiteral("integration_id = ?"), {integrationId}};
    QSet<QString> seen;

    for (const RemoteTask &remote : active) {
        if (seen.contains(remote.externalId))
            continue; // the service listed it twice (pagination overlap)
        seen.insert(remote.externalId);
        const QString hash = remote.hash();
        const auto it = existing.constFind(remote.externalId);

        if (it == existing.cend()) {
            Task t;
            t.id = newId();
            t.title = remote.title.trimmed().isEmpty() ? QStringLiteral("(untitled)") : remote.title.trimmed();
            t.description = remote.description;
            t.priority = remote.priority;
            t.dueDate = remote.dueDate;
            t.tags = remote.tags;
            t.categoryId = category ? category->id : QString();
            parts.append(ops::createTask(t, now));
            if (!remote.url.isEmpty()) {
                parts.append(raw({{QStringLiteral("task_actions"), QStringLiteral("task_id = ?"), {t.id}}},
                                 QStringLiteral("INSERT INTO task_actions (id, task_id, kind, target, run_on, sort_order, created_at) "
                                                "VALUES (?, ?, 'url', ?, 'manual', 0, ?)"),
                                 {newId(), t.id, remote.url, now}));
            }
            parts.append(raw({linkScope},
                             QStringLiteral("INSERT INTO synced_items (id, integration_id, external_id, task_id, data_hash, "
                                            "last_synced_at, sync_status, created_at) VALUES (?, ?, ?, ?, ?, ?, 'synced', ?)"),
                             {newId(), integrationId, remote.externalId, t.id, hash, now, now}));
            ++r.created;
            continue;
        }

        const Link &link = *it;
        const auto task = link.taskId.isEmpty() ? std::nullopt : q.task(link.taskId);
        if (!task || link.hash == hash) {
            // Deleted here (leave it deleted), or nothing new from the service.
            if (link.status == QLatin1String("missing_external"))
                parts.append(raw({linkScope}, QStringLiteral("UPDATE synced_items SET sync_status = 'synced' WHERE id = ?"), {link.id}));
            ++r.unchanged;
            continue;
        }

        // Changed there: the service's fields win; tags added here are kept.
        QStringList tags = task->tags;
        for (const QString &tag : remote.tags) {
            if (!tags.contains(tag, Qt::CaseInsensitive))
                tags.append(tag);
        }
        QString why;
        auto update = ops::updateTask(q, task->id,
                                      {{QStringLiteral("title"), remote.title.trimmed().isEmpty() ? task->title : remote.title},
                                       {QStringLiteral("description"), remote.description},
                                       {QStringLiteral("priority"), nameOf(remote.priority)},
                                       {QStringLiteral("due_date"), remote.dueDate.isValid() ? remote.dueDate.toString(Qt::ISODate) : QString()},
                                       {QStringLiteral("tags"), tags}},
                                      now, &why);
        if (!update) {
            setError(error, why);
            return std::nullopt;
        }
        parts.append(*update);
        QString status = link.status == QLatin1String("done_pushed") ? QStringLiteral("done_pushed") : QStringLiteral("synced");
        // Reopened there after we finished it here: reopen it here too.
        if (remote.open && link.status == QLatin1String("done_pushed")
            && (task->status == TaskStatus::Completed || task->status == TaskStatus::Cancelled)) {
            parts.append(raw({{QStringLiteral("tasks"), QStringLiteral("id = ?"), {task->id}}},
                             QStringLiteral("UPDATE tasks SET status = 'pending', updated_at = ? WHERE id = ?"), {now, task->id}));
            status = QStringLiteral("synced");
        }
        parts.append(raw({linkScope}, QStringLiteral("UPDATE synced_items SET data_hash = ?, last_synced_at = ?, sync_status = ? WHERE id = ?"),
                         {hash, now, status, link.id}));
        ++r.updated;
    }

    for (const Link &link : existing) {
        if (seen.contains(link.externalId))
            continue;
        const Gone what = gone.value(link.externalId, Gone::Unknown);
        if (what == Gone::Unknown)
            continue; // ask again next time
        const auto task = link.taskId.isEmpty() ? std::nullopt : q.task(link.taskId);
        const Mutation unlink = raw({linkScope}, QStringLiteral("DELETE FROM synced_items WHERE id = ?"), {link.id});
        const bool alreadyClosed = task && (task->status == TaskStatus::Completed || task->status == TaskStatus::Cancelled);
        if (!task || alreadyClosed || link.status == QLatin1String("done_pushed")) {
            parts.append(unlink);
            continue;
        }
        const TaskStatus to = what == Gone::Completed ? TaskStatus::Completed : TaskStatus::Cancelled;
        QString why;
        auto close = ops::setTaskStatus(q, task->id, to, now, &why);
        if (!close) {
            setError(error, why);
            return std::nullopt;
        }
        parts.append(*close);
        const QString reason = what == Gone::Completed ? QStringLiteral("completed there")
            : what == Gone::Cancelled                  ? QStringLiteral("cancelled or deleted there")
                                                       : QStringLiteral("no longer assigned to you");
        parts.append(raw({{QStringLiteral("task_history"), QStringLiteral("task_id = ?"), {task->id}}},
                         QStringLiteral("INSERT INTO task_history (id, task_id, action, at, details) VALUES (?, ?, 'sync_closed', ?, ?)"),
                         {newId(), task->id, now, historyJson({{QStringLiteral("reason"), reason}})}));
        parts.append(unlink);
        if (what == Gone::Completed)
            r.completedThere.insert(task->id, link.externalId);
        r.notes.append(QStringLiteral("“%1”: %2").arg(task->title, reason));
        ++r.closed;
    }

    if (result)
        *result = r;
    return combine(QStringLiteral("Sync"), parts);
}

std::optional<Mutation> logWorkedThere(const Queries &q, const QString &taskId, const Worked &worked, const QString &service,
                                       const QTimeZone &zone, qint64 now, int *minutes)
{
    constexpr qint64 kMinute = 60 * 1000;
    const auto task = q.task(taskId);
    if (!task || task->isRecurring() || worked.completedAt <= 0)
        return std::nullopt;
    if (!q.workLogsForTask(taskId).isEmpty())
        return std::nullopt; // the timer has it

    const qint64 end = std::min(worked.completedAt, now);
    qint64 start = end - qint64(std::max(5, task->estimateMinutes)) * kMinute;
    if (worked.startedAt > start && worked.startedAt < end)
        start = std::min(worked.startedAt, end - 5 * kMinute); // picked up and closed quickly

    const QDate day = QDateTime::fromMSecsSinceEpoch(end, zone).date();
    const qint64 dayStart = QDateTime(day, QTime(0, 0), zone).toMSecsSinceEpoch();
    const qint64 dayEnd = QDateTime(day.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();
    QString blockId;
    for (const ScheduleBlock &s : q.schedulesForTask(taskId)) {
        if (s.end <= dayStart || s.start >= dayEnd)
            continue;
        if (s.completed)
            return std::nullopt; // already shown as done that day
        if (blockId.isEmpty())
            blockId = s.id;
    }

    const QString logId = newId();
    const QString notes = QStringLiteral("Closed in %1").arg(service);
    const int length = int((end - start + kMinute / 2) / kMinute);
    QList<Mutation> parts{raw({{QStringLiteral("work_logs"), QStringLiteral("id = ?"), {logId}}},
                              QStringLiteral("INSERT INTO work_logs (id, task_id, started_at, ended_at, duration_minutes, notes, created_at) "
                                             "VALUES (?, ?, ?, ?, ?, ?, ?)"),
                              {logId, taskId, start, end, length, notes, now})};
    const Scope blocks{QStringLiteral("schedules"), QStringLiteral("task_id = ?"), {taskId}};
    if (blockId.isEmpty()) {
        parts.append(raw({blocks},
                         QStringLiteral("INSERT INTO schedules (id, task_id, scheduled_start, scheduled_end, actual_start, actual_end, "
                                        "completed, work_log_id, created_at) VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?)"),
                         {newId(), taskId, start, end, start, end, logId, now}));
    } else {
        parts.append(raw({blocks},
                         QStringLiteral("UPDATE schedules SET completed = 1, scheduled_start = ?, scheduled_end = ?, actual_start = ?, "
                                        "actual_end = ?, work_log_id = ? WHERE id = ?"),
                         {start, end, start, end, logId, blockId}));
    }
    if (minutes)
        *minutes = length;
    return combine(QStringLiteral("Log work on “%1”").arg(task->title), parts);
}

QList<PendingPush> pendingCompletions(const Queries &q, const QString &integrationId)
{
    QList<PendingPush> out;
    QSqlQuery s(q.db());
    s.prepare(QStringLiteral("SELECT si.external_id, t.id, t.title FROM synced_items si JOIN tasks t ON t.id = si.task_id "
                             "WHERE si.integration_id = ? AND si.sync_status != 'done_pushed' AND t.status = 'completed' "
                             "ORDER BY t.updated_at"));
    s.addBindValue(integrationId);
    s.exec();
    while (s.next())
        out.append({s.value(0).toString(), s.value(1).toString(), s.value(2).toString()});
    return out;
}

Mutation markPushed(const QString &integrationId, const QString &externalId)
{
    Mutation m = raw({{QStringLiteral("synced_items"), QStringLiteral("integration_id = ? AND external_id = ?"), {integrationId, externalId}}},
                     QStringLiteral("UPDATE synced_items SET sync_status = 'done_pushed' WHERE integration_id = ? AND external_id = ?"),
                     {integrationId, externalId});
    m.text = QStringLiteral("Sync");
    return m;
}

// ---- calendars ---------------------------------------------------------------

QString RemoteEvent::hash() const
{
    const Event &e = event;
    return sha1({e.title, e.description, e.location, e.allDay ? QStringLiteral("1") : QStringLiteral("0"),
                 e.startDate.toString(Qt::ISODate), e.endDate.toString(Qt::ISODate), QString::number(e.startAt),
                 QString::number(e.endAt), nameOf(e.blocking.mode), e.blocking.allowedTags.join(QLatin1Char(',')), e.color});
}

std::optional<Mutation> pullFeed(const Queries &q, const QString &integrationId, const QString &feedId,
                                 const QList<RemoteEvent> &events, qint64 from, qint64 to, const QString &timezone,
                                 qint64 now, FeedResult *result, QString *error)
{
    struct Existing {
        QString linkId;
        QString eventId;
        QString hash;
        bool inWindow = false;
        bool allDay = false;
        qint64 start = 0;
        qint64 end = 0;
        QString title;
        QString location;
    };
    // External ids are only unique per integration, and two feeds can share
    // an event (the same calendar twice, an invite in both): prefix the feed.
    const QString prefix = feedId + QLatin1Char('/');
    const QDate firstDay = QDateTime::fromMSecsSinceEpoch(from, QTimeZone(timezone.toUtf8())).date();
    QHash<QString, Existing> existing;
    QSqlQuery s(q.db());
    s.prepare(QStringLiteral("SELECT se.id, se.external_event_id, se.event_id, se.data_hash, e.is_all_day, e.start_at, e.end_date, e.end_at, "
                             "e.title, e.location "
                             "FROM synced_events se JOIN events e ON e.id = se.event_id "
                             "WHERE se.integration_id = ? AND se.external_calendar_id = ?"));
    s.addBindValue(integrationId);
    s.addBindValue(feedId);
    s.exec();
    while (s.next()) {
        const bool allDay = s.value(4).toBool();
        const bool inWindow = allDay ? QDate::fromString(s.value(6).toString(), Qt::ISODate) >= firstDay
                                     : (s.value(5).toLongLong() >= from && s.value(5).toLongLong() < to);
        existing.insert(s.value(1).toString(), {s.value(0).toString(), s.value(2).toString(), s.value(3).toString(), inWindow, allDay,
                                                s.value(5).toLongLong(), s.value(7).toLongLong(), s.value(8).toString(),
                                                s.value(9).toString()});
    }
    // A feed's first pull brings everything in at once: none of it is news.
    const bool firstPull = existing.isEmpty();
    const auto change = [&](CalendarChange::Kind kind, const QString &key, const QString &eventId, const QString &title,
                            const QString &location, qint64 start, qint64 end) {
        CalendarChange c;
        c.key = key;
        c.kind = kind;
        c.eventId = eventId;
        c.title = title;
        c.location = location;
        c.start = start;
        c.end = end;
        c.seenAt = now;
        return c;
    };

    FeedResult r;
    QList<Mutation> parts;
    QSet<QString> seen;
    const Scope linkScope{QStringLiteral("synced_events"), QStringLiteral("integration_id = ? AND external_calendar_id = ?"),
                          {integrationId, feedId}};
    for (const RemoteEvent &remote : events) {
        const QString externalId = prefix + remote.externalId;
        if (seen.contains(externalId))
            continue;
        seen.insert(externalId);
        const QString hash = remote.hash();
        const auto it = existing.constFind(externalId);
        if (it != existing.cend() && it->hash == hash)
            continue;
        Event e = remote.event;
        e.id = it != existing.cend() ? it->eventId : newId();
        if (e.title.trimmed().isEmpty())
            e.title = QStringLiteral("(busy)");
        QString why;
        auto save = ops::saveEvent(e, it == existing.cend(), timezone, now, &why);
        if (!save)
            continue; // a malformed event in someone else's feed shouldn't stop the rest
        parts.append(*save);
        if (it == existing.cend()) {
            parts.append(raw({linkScope},
                             QStringLiteral("INSERT INTO synced_events (id, integration_id, external_calendar_id, external_event_id, "
                                            "event_id, data_hash, last_synced_at, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
                             {newId(), integrationId, feedId, externalId, e.id, hash, now, now}));
            ++r.created;
            if (!firstPull && !e.allDay && e.endAt > now)
                r.changes.append(change(CalendarChange::Kind::Added, externalId, e.id, e.title, e.location, e.startAt, e.endAt));
        } else {
            parts.append(raw({linkScope}, QStringLiteral("UPDATE synced_events SET data_hash = ?, last_synced_at = ? WHERE id = ?"),
                             {hash, now, it->linkId}));
            ++r.updated;
            // What matters to someone's day: the time, and where to go.
            if (!e.allDay && !it->allDay && std::max(e.endAt, it->end) > now) {
                CalendarChange c = change(CalendarChange::Kind::Moved, externalId, e.id, e.title, e.location, e.startAt, e.endAt);
                if (e.location != it->location)
                    c.oldLocation = it->location;
                if (e.startAt != it->start || e.endAt != it->end) {
                    c.oldStart = it->start;
                    c.oldEnd = it->end;
                    r.changes.append(c);
                } else if (e.location != it->location) {
                    c.kind = CalendarChange::Kind::Changed;
                    r.changes.append(c);
                }
            }
        }
    }
    for (auto it = existing.cbegin(); it != existing.cend(); ++it) {
        if (seen.contains(it.key()) || !it->inWindow)
            continue;
        parts.append(raw({{QStringLiteral("events"), QStringLiteral("id = ?"), {it->eventId}}, linkScope},
                         QStringLiteral("DELETE FROM events WHERE id = ?"), {it->eventId}));
        ++r.removed;
        if (!it->allDay && it->end > now)
            r.changes.append(change(CalendarChange::Kind::Cancelled, it.key(), {}, it->title, it->location, it->start, it->end));
    }
    if (result)
        *result = r;
    Q_UNUSED(error)
    return combine(QStringLiteral("Calendar sync"), parts);
}

Mutation dropFeed(const QString &integrationId, const QString &feedId)
{
    const QString owned = QStringLiteral("id IN (SELECT event_id FROM synced_events WHERE integration_id = ? AND external_calendar_id = ?)");
    Mutation m = raw({{QStringLiteral("events"), owned, {integrationId, feedId}},
                      {QStringLiteral("synced_events"), QStringLiteral("integration_id = ? AND external_calendar_id = ?"), {integrationId, feedId}}},
                     QStringLiteral("DELETE FROM events WHERE ") + owned, {integrationId, feedId});
    m.text = QStringLiteral("Remove calendar");
    return m;
}

QString feedOf(const Queries &q, const QString &eventId)
{
    QSqlQuery s(q.db());
    s.prepare(QStringLiteral("SELECT external_calendar_id FROM synced_events WHERE event_id = ?"));
    s.addBindValue(eventId);
    return s.exec() && s.next() ? s.value(0).toString() : QString();
}

QDate dayIn(const QDate &day, const QTimeZone &calendarZone, const QTimeZone &local)
{
    return QDateTime(day, QTime(12, 0), calendarZone).toTimeZone(local).date();
}

QList<RemoteEvent> moveAllDay(QList<RemoteEvent> events, const QTimeZone &calendarZone, const QTimeZone &local)
{
    if (!calendarZone.isValid() || calendarZone == local)
        return events;
    for (RemoteEvent &r : events) {
        if (!r.event.allDay)
            continue;
        r.event.startDate = dayIn(r.event.startDate, calendarZone, local);
        r.event.endDate = std::max(r.event.startDate, dayIn(r.event.endDate, calendarZone, local));
    }
    return events;
}

} // namespace tumbler::sync
