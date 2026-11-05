#include "database.h"
#include "operations.h"
#include "queries.h"
#include "sync.h"

#include <QSqlQuery>
#include <QTest>
#include <QTimeZone>

using namespace tumbler;
using namespace tumbler::sync;

namespace {

constexpr qint64 kNow = 1'770'000'000'000;
constexpr qint64 kHour = 3'600'000;

QVariant scalar(QSqlDatabase db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next())
        return {};
    return q.value(0);
}

RemoteTask issue(const QString &id, const QString &title)
{
    RemoteTask r;
    r.externalId = id;
    r.title = title;
    r.priority = Priority::High;
    r.dueDate = QDate(2026, 10, 30);
    r.tags = {"linear", "linear:ENG"};
    r.url = "https://linear.app/acme/issue/" + id;
    return r;
}

RemoteEvent meeting(const QString &uid, qint64 start, const QString &title = "Standup")
{
    RemoteEvent r;
    r.externalId = uid;
    r.event.title = title;
    r.event.startAt = start;
    r.event.endAt = start + kHour / 2;
    r.event.blocking.mode = BlockingMode::BlockAll;
    r.event.color = "#22c55e";
    return r;
}

} // namespace

class TestSync : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    QString m_linear;
    QSqlDatabase db() const { return m_db->handle(); }
    Queries q() const { return Queries(db()); }

    void run(const std::optional<Mutation> &m)
    {
        QVERIFY(m);
        QString error;
        QVERIFY2(MutationRunner(db()).run(*m, &error), qPrintable(error));
    }

    PullResult pull(const QList<RemoteTask> &active, const QHash<QString, Gone> &gone = {})
    {
        PullResult r;
        run(pullTasks(q(), m_linear, active, gone, kNow, &r));
        return r;
    }

    QString taskFor(const QString &externalId)
    {
        return scalar(db(), "SELECT task_id FROM synced_items WHERE external_id = '" + externalId + "'").toString();
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(":memory:") && m_db->migrate());
        m_linear = *ensureIntegration(db(), "linear", kNow);
        QCOMPARE(*ensureIntegration(db(), "linear", kNow), m_linear); // idempotent
    }

    void newIssuesBecomeTasks()
    {
        const PullResult r = pull({issue("ENG-1", "Fix login"), issue("ENG-2", "Write docs")});
        QCOMPARE(r.created, 2);
        const auto t = q().task(taskFor("ENG-1"));
        QVERIFY(t);
        QCOMPARE(t->title, QStringLiteral("Fix login"));
        QCOMPARE(t->priority, Priority::High);
        QCOMPARE(t->dueDate, QDate(2026, 10, 30));
        QCOMPARE(t->tags, QStringList({"linear", "linear:ENG"}));
        const auto actions = q().actions(t->id);
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.first().target, QStringLiteral("https://linear.app/acme/issue/ENG-1"));
        QVERIFY(!actions.first().onStart); // a button, not something that opens on every start

        // Pulling the same thing again changes nothing.
        const PullResult again = pull({issue("ENG-1", "Fix login"), issue("ENG-2", "Write docs")});
        QCOMPARE(again.created, 0);
        QCOMPARE(again.unchanged, 2);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM tasks").toInt(), 2);
    }

    void remoteChangesWinButLocalTagsStay()
    {
        pull({issue("ENG-1", "Fix login")});
        const QString id = taskFor("ENG-1");
        run(ops::updateTask(q(), id, {{"title", "my wording"}, {"tags", QStringList{"linear", "linear:ENG", "deep"}}}, kNow));

        // Untouched remotely: the local edit stands.
        pull({issue("ENG-1", "Fix login")});
        QCOMPARE(q().task(id)->title, QStringLiteral("my wording"));

        RemoteTask changed = issue("ENG-1", "Fix login on Safari");
        changed.tags.append("bug");
        QCOMPARE(pull({changed}).updated, 1);
        QCOMPARE(q().task(id)->title, QStringLiteral("Fix login on Safari"));
        QCOMPARE(q().task(id)->tags, QStringList({"linear", "linear:ENG", "deep", "bug"}));
    }

    void goneItemsCloseAndUnlink()
    {
        pull({issue("ENG-1", "a"), issue("ENG-2", "b"), issue("ENG-3", "c"), issue("ENG-4", "d")});
        const QString a = taskFor("ENG-1"), b = taskFor("ENG-2"), c = taskFor("ENG-3"), d = taskFor("ENG-4");
        QCOMPARE(missing(q(), m_linear, {}), QStringList({"ENG-1", "ENG-2", "ENG-3", "ENG-4"}));

        const PullResult r = pull({}, {{"ENG-1", Gone::Completed}, {"ENG-2", Gone::Cancelled}, {"ENG-3", Gone::Elsewhere}});
        QCOMPARE(r.closed, 3);
        QCOMPARE(r.notes.size(), 3);
        QCOMPARE(q().task(a)->status, TaskStatus::Completed);
        QCOMPARE(q().task(b)->status, TaskStatus::Cancelled);
        QCOMPARE(q().task(c)->status, TaskStatus::Cancelled);
        // Unknown (couldn't ask): left alone and still linked.
        QCOMPARE(q().task(d)->status, TaskStatus::Pending);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM synced_items").toInt(), 1);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_history WHERE action = 'sync_closed'").toInt(), 3);
    }

    void doneThereIsReportedForLogging()
    {
        pull({issue("ENG-1", "a"), issue("ENG-2", "b")});
        const QString a = taskFor("ENG-1");
        const PullResult r = pull({}, {{"ENG-1", Gone::Completed}, {"ENG-2", Gone::Cancelled}});
        QCOMPARE(r.completedThere, (QHash<QString, QString>{{a, "ENG-1"}})); // cancelled isn't work done
    }

    void workedThereBecomesABlock()
    {
        const QTimeZone zone("Australia/Sydney");
        constexpr qint64 kMin = 60'000;
        const qint64 done = kNow - 2 * kHour;
        pull({issue("ENG-1", "quick"), issue("ENG-2", "planned"), issue("ENG-3", "timed"), issue("ENG-4", "estimate")});
        const QString quick = taskFor("ENG-1"), planned = taskFor("ENG-2"), timed = taskFor("ENG-3"), estimate = taskFor("ENG-4");
        run(ops::updateTask(q(), estimate, {{"time_estimate_minutes", 45}}, kNow));
        run(ops::scheduleTask("block", planned, done + kHour, done + 2 * kHour, kNow)); // planned for later that day
        run(ops::logWork(timed, "log", done - kHour, done - kHour / 2, {}, kNow));

        // Started there 10 minutes before it was done: 10 minutes, not the 30 estimate.
        int minutes = 0;
        run(logWorkedThere(q(), quick, {done - 10 * kMin, done}, "Linear", zone, kNow, &minutes));
        QCOMPARE(minutes, 10);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM schedules WHERE completed = 1 AND task_id = '" + quick + "'").toInt(), 1);
        QCOMPARE(scalar(db(), "SELECT scheduled_start FROM schedules WHERE task_id = '" + quick + "'").toLongLong(), done - 10 * kMin);
        QCOMPARE(scalar(db(), "SELECT notes FROM work_logs WHERE task_id = '" + quick + "'").toString(), QStringLiteral("Closed in Linear"));

        // Planned that day: the block moves to when it was done.
        run(logWorkedThere(q(), planned, {0, done}, "Linear", zone, kNow, &minutes));
        QCOMPARE(minutes, 30);
        QCOMPARE(q().schedulesForTask(planned).size(), 1);
        const ScheduleBlock b = q().schedulesForTask(planned).first();
        QVERIFY(b.completed);
        QCOMPARE(b.start, done - 30 * kMin);
        QCOMPARE(b.end, done);
        QVERIFY(!b.workLogId.isEmpty());

        // Started days ago: as long as its estimate.
        run(logWorkedThere(q(), estimate, {done - 72 * kHour, done}, "Linear", zone, kNow, &minutes));
        QCOMPARE(minutes, 45);

        // The timer has it; and a second sync doesn't log it twice.
        QVERIFY(!logWorkedThere(q(), timed, {0, done}, "Linear", zone, kNow));
        QVERIFY(!logWorkedThere(q(), quick, {0, done}, "Linear", zone, kNow));
        QVERIFY(!logWorkedThere(q(), quick, {0, 0}, "Linear", zone, kNow)); // no completion time
    }

    void finishedBetweenSyncsComeInDone()
    {
        constexpr qint64 kMin = 60'000;
        const qint64 done = kNow - kHour;
        pull({issue("ENG-1", "linked")});
        const auto finished = [&](const QString &id, const QString &title, qint64 started) {
            return FinishedRemote{issue(id, title), {started, done}};
        };
        int created = 0, minutes = 0;
        run(pullFinished(q(), m_linear,
                         {finished("ENG-1", "linked", 0), finished("ENG-9", "Hotfix", done - 20 * kMin), finished("ENG-9", "Hotfix", 0)},
                         "Linear", kNow, &created, &minutes));
        QCOMPARE(created, 1); // the linked one is the pull's to close; the repeat is skipped
        QCOMPARE(minutes, 20);
        const QString id = scalar(db(), "SELECT id FROM tasks WHERE title = 'Hotfix'").toString();
        QCOMPARE(q().task(id)->status, TaskStatus::Completed);
        QCOMPARE(q().task(id)->tags, QStringList({"linear", "linear:ENG"}));
        QCOMPARE(scalar(db(), "SELECT at FROM task_history WHERE action = 'completed' AND task_id = '" + id + "'").toLongLong(), done);
        QCOMPARE(scalar(db(), "SELECT target FROM task_actions WHERE task_id = '" + id + "'").toString(),
                 QStringLiteral("https://linear.app/acme/issue/ENG-9"));
        const ScheduleBlock b = q().schedulesForTask(id).first();
        QVERIFY(b.completed);
        QCOMPARE(b.start, done - 20 * kMin);
        QCOMPARE(b.end, done);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM synced_items WHERE external_id = 'ENG-9'").toInt(), 0); // closed, so not linked

        // The next sync overlaps a little: already here, so nothing new.
        QVERIFY(!pullFinished(q(), m_linear, {finished("ENG-9", "Hotfix", 0)}, "Linear", kNow));
        // Nor one that was linked and closed by an earlier pull (its link is gone, its task opens it).
        run(pullTasks(q(), m_linear, {}, {{"ENG-1", Gone::Completed}}, kNow, nullptr));
        QVERIFY(!pullFinished(q(), m_linear, {finished("ENG-1", "linked", 0)}, "Linear", kNow));
    }

    void deletedHereStaysDeleted()
    {
        pull({issue("ENG-1", "noisy")});
        const QString id = taskFor("ENG-1");
        run(ops::deleteTask(id));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM synced_items WHERE task_id IS NULL").toInt(), 1);

        RemoteTask changed = issue("ENG-1", "noisy, now louder");
        const PullResult r = pull({changed});
        QCOMPARE(r.created, 0);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM tasks").toInt(), 0);

        // Once it's gone there too, the tombstone goes.
        pull({}, {{"ENG-1", Gone::Completed}});
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM synced_items").toInt(), 0);
    }

    void deleteUndoRestoresTheLink()
    {
        pull({issue("ENG-1", "x")});
        const QString id = taskFor("ENG-1");
        MutationRunner runner(db());
        const auto applied = runner.run(ops::deleteTask(id));
        QVERIFY(applied);
        QVERIFY(runner.revert(*applied));
        QCOMPARE(taskFor("ENG-1"), id);
    }

    void completionsArePushedOnce()
    {
        pull({issue("ENG-1", "a"), issue("ENG-2", "b")});
        const QString a = taskFor("ENG-1");
        QVERIFY(pendingCompletions(q(), m_linear).isEmpty());
        run(ops::setTaskStatus(q(), a, TaskStatus::Completed, kNow));
        const auto pending = pendingCompletions(q(), m_linear);
        QCOMPARE(pending.size(), 1);
        QCOMPARE(pending.first().externalId, QStringLiteral("ENG-1"));
        run(markPushed(m_linear, "ENG-1"));
        QVERIFY(pendingCompletions(q(), m_linear).isEmpty());

        // The service now leaves it out: unlink quietly, the task is already done.
        PullResult r = pull({issue("ENG-2", "b")}, {{"ENG-1", Gone::Completed}});
        QCOMPARE(r.closed, 0);
        QCOMPARE(q().task(a)->status, TaskStatus::Completed);
        QVERIFY(taskFor("ENG-1").isEmpty());
    }

    void reopenedThereReopensHere()
    {
        pull({issue("ENG-1", "a")});
        const QString a = taskFor("ENG-1");
        run(ops::setTaskStatus(q(), a, TaskStatus::Completed, kNow));
        run(markPushed(m_linear, "ENG-1"));
        // Someone moved it back to In Progress (and it changed).
        RemoteTask back = issue("ENG-1", "a, again");
        pull({back});
        QCOMPARE(q().task(a)->status, TaskStatus::Pending);
        QCOMPARE(pendingCompletions(q(), m_linear).size(), 0);
        run(ops::setTaskStatus(q(), a, TaskStatus::Completed, kNow));
        QCOMPARE(pendingCompletions(q(), m_linear).size(), 1); // pushed again next time
    }

    void migrationKeepsImportedLinks()
    {
        // 002 rebuilt synced_items; the column set and the cascade must match.
        QCOMPARE(scalar(db(), "PRAGMA user_version").toInt(), 2);
        QSqlQuery fk(db());
        QVERIFY(fk.exec("PRAGMA foreign_key_list(synced_items)"));
        QStringList rules;
        while (fk.next())
            rules.append(fk.value(2).toString() + ":" + fk.value(6).toString());
        rules.sort();
        QCOMPARE(rules, QStringList({"integrations:CASCADE", "tasks:SET NULL"}));
    }

    // ---- calendar feeds ----

    void feedEventsComeAndGo()
    {
        const QString ics = *ensureIntegration(db(), "ics", kNow);
        const qint64 from = kNow, to = kNow + 30 * 24 * kHour;
        FeedResult r;
        run(pullFeed(q(), ics, "work", {meeting("a", kNow + kHour), meeting("b", kNow + 2 * kHour, "Review")}, from, to, "Europe/London", kNow, &r));
        QCOMPARE(r.created, 2);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM events WHERE blocking_mode = 'block_all'").toInt(), 2);
        const QString eventA = scalar(db(), "SELECT event_id FROM synced_events WHERE external_event_id = 'work/a'").toString();
        QCOMPARE(feedOf(q(), eventA), QStringLiteral("work"));

        // Same uid from another feed is a different event.
        run(pullFeed(q(), ics, "home", {meeting("a", kNow + kHour)}, from, to, "Europe/London", kNow, &r));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM events").toInt(), 3);

        // a moved, b cancelled.
        run(pullFeed(q(), ics, "work", {meeting("a", kNow + 3 * kHour)}, from, to, "Europe/London", kNow, &r));
        QCOMPARE(r.updated, 1);
        QCOMPARE(r.removed, 1);
        QCOMPARE(q().event(eventA)->startAt, kNow + 3 * kHour);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM events").toInt(), 2);

        // Past events outside the window stay as history.
        run(pullFeed(q(), ics, "work", {}, kNow + 5 * kHour, to, "Europe/London", kNow, &r));
        QCOMPARE(r.removed, 0);

        run(dropFeed(ics, "work"));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM events").toInt(), 1);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM synced_events").toInt(), 1);
    }

    void allDayFeedEvents()
    {
        const QString ics = *ensureIntegration(db(), "ics", kNow);
        RemoteEvent holiday;
        holiday.externalId = "hol";
        holiday.event.title = "Bank holiday";
        holiday.event.allDay = true;
        holiday.event.startDate = QDate(2026, 12, 25);
        holiday.event.endDate = QDate(2026, 12, 25);
        FeedResult r;
        run(pullFeed(q(), ics, "uk", {holiday}, kNow, kNow + 400 * 24 * kHour, "Europe/London", kNow, &r));
        QCOMPARE(r.created, 1);
        run(pullFeed(q(), ics, "uk", {holiday}, kNow, kNow + 400 * 24 * kHour, "Europe/London", kNow, &r));
        QCOMPARE(r.created + r.updated + r.removed, 0);
        run(pullFeed(q(), ics, "uk", {}, kNow, kNow + 400 * 24 * kHour, "Europe/London", kNow, &r));
        QCOMPARE(r.removed, 1);
        QVERIFY(r.changes.isEmpty()); // all-day events don't raise alerts
    }

    void allDayEventsInAnotherZone()
    {
        const QTimeZone sydney("Australia/Sydney"), newYork("America/New_York"), london("Europe/London");
        // Election day in the US is the next day in Australia...
        QCOMPARE(dayIn(QDate(2026, 11, 3), newYork, sydney), QDate(2026, 11, 4));
        // ...and an Australian holiday is the day before in the US.
        QCOMPARE(dayIn(QDate(2026, 1, 26), sydney, newYork), QDate(2026, 1, 25));
        // A few hours apart, the day stays.
        QCOMPARE(dayIn(QDate(2026, 11, 3), london, newYork), QDate(2026, 11, 3));

        RemoteEvent trip;
        trip.event.allDay = true;
        trip.event.startDate = QDate(2026, 11, 26);
        trip.event.endDate = QDate(2026, 11, 29);
        RemoteEvent call;
        call.event.startAt = kNow;
        call.event.endAt = kNow + kHour;
        const auto moved = moveAllDay({trip, call}, newYork, sydney);
        QCOMPARE(moved[0].event.startDate, QDate(2026, 11, 27));
        QCOMPARE(moved[0].event.endDate, QDate(2026, 11, 30));
        QCOMPARE(moved[1].event.startAt, kNow); // timed events already know their zone
        QCOMPARE(moveAllDay({trip}, sydney, sydney)[0].event.startDate, QDate(2026, 11, 26));
        QCOMPARE(moveAllDay({trip}, QTimeZone(), sydney)[0].event.startDate, QDate(2026, 11, 26));
    }

    void feedReportsChangesThatMatter()
    {
        using Kind = CalendarChange::Kind;
        const QString ics = *ensureIntegration(db(), "ics", kNow);
        const qint64 from = kNow - 24 * kHour, to = kNow + 30 * 24 * kHour;
        FeedResult r;
        // First pull: everything is new, none of it news.
        run(pullFeed(q(), ics, "work", {meeting("standup", kNow + kHour), meeting("old", kNow - 3 * kHour, "Earlier"),
                                         meeting("planning", kNow + 4 * kHour, "Planning")},
                     from, to, "Europe/London", kNow, &r));
        QCOMPARE(r.created, 3);
        QVERIFY(r.changes.isEmpty());

        // Invited to something; planning moved; standup's room changed; a
        // past meeting edited (not news).
        RemoteEvent standup = meeting("standup", kNow + kHour);
        standup.event.location = "Room 2";
        RemoteEvent old = meeting("old", kNow - 3 * kHour, "Earlier (notes added)");
        run(pullFeed(q(), ics, "work", {standup, old, meeting("planning", kNow + 5 * kHour, "Planning"), meeting("sync", kNow + kHour / 2, "Design sync")},
                     from, to, "Europe/London", kNow, &r));
        QCOMPARE(r.changes.size(), 3);
        QHash<QString, CalendarChange> byKey;
        for (const CalendarChange &c : r.changes)
            byKey.insert(c.key, c);
        QCOMPARE(byKey["work/sync"].kind, Kind::Added);
        QCOMPARE(byKey["work/sync"].title, QStringLiteral("Design sync"));
        QVERIFY(!byKey["work/sync"].eventId.isEmpty());
        QCOMPARE(byKey["work/planning"].kind, Kind::Moved);
        QCOMPARE(byKey["work/planning"].oldStart, kNow + 4 * kHour);
        QCOMPARE(byKey["work/planning"].start, kNow + 5 * kHour);
        QCOMPARE(byKey["work/standup"].kind, Kind::Changed);
        QCOMPARE(byKey["work/standup"].location, QStringLiteral("Room 2"));

        // Cancelled.
        run(pullFeed(q(), ics, "work", {standup, old, meeting("planning", kNow + 5 * kHour, "Planning")}, from, to, "Europe/London", kNow, &r));
        QCOMPARE(r.changes.size(), 1);
        QCOMPARE(r.changes[0].kind, Kind::Cancelled);
        QCOMPARE(r.changes[0].title, QStringLiteral("Design sync"));
        QVERIFY(r.changes[0].eventId.isEmpty());
    }

    void changesMergePerEvent()
    {
        using Kind = CalendarChange::Kind;
        const auto c = [](Kind kind, qint64 start, qint64 oldStart = 0) {
            CalendarChange x;
            x.key = "work/a";
            x.kind = kind;
            x.title = "Review";
            x.start = start;
            x.end = start + kHour;
            if (oldStart) {
                x.oldStart = oldStart;
                x.oldEnd = oldStart + kHour;
            }
            return x;
        };
        // New, then moved: still new, at the new time.
        auto kept = mergeChanges({}, {c(Kind::Added, kNow + kHour)}, kNow);
        kept = mergeChanges(kept, {c(Kind::Moved, kNow + 2 * kHour, kNow + kHour)}, kNow);
        QCOMPARE(kept.size(), 1);
        QCOMPARE(kept[0].kind, Kind::Added);
        QCOMPARE(kept[0].start, kNow + 2 * kHour);

        // Moved twice: from the first time to the last...
        kept = mergeChanges({}, {c(Kind::Moved, kNow + 2 * kHour, kNow + kHour)}, kNow);
        kept = mergeChanges(kept, {c(Kind::Moved, kNow + 3 * kHour, kNow + 2 * kHour)}, kNow);
        QCOMPARE(kept[0].oldStart, kNow + kHour);
        QCOMPARE(kept[0].start, kNow + 3 * kHour);
        // ...and back again: nothing to say.
        kept = mergeChanges(kept, {c(Kind::Moved, kNow + kHour, kNow + 3 * kHour)}, kNow);
        QVERIFY(kept.isEmpty());

        // Cancelled replaces whatever came before.
        kept = mergeChanges({c(Kind::Added, kNow + kHour)}, {c(Kind::Cancelled, kNow + kHour)}, kNow);
        QCOMPARE(kept[0].kind, Kind::Cancelled);

        // Over: dropped.
        QVERIFY(mergeChanges(kept, {}, kNow + 3 * kHour).isEmpty());

        // Saved and read back.
        kept = mergeChanges({}, {c(Kind::Moved, kNow + 2 * kHour, kNow + kHour)}, kNow);
        const auto back = changesFromJson(toJson(kept));
        QCOMPARE(back.size(), 1);
        QCOMPARE(back[0].kind, Kind::Moved);
        QCOMPARE(back[0].oldStart, kNow + kHour);

        // Urgency counts from the earlier of the old and new times.
        QCOMPARE(urgencyOf(c(Kind::Added, kNow + 20 * 60000), kNow), ChangeUrgency::Soon);
        QCOMPARE(urgencyOf(c(Kind::Added, kNow - 10 * 60000), kNow), ChangeUrgency::Soon); // under way
        QCOMPARE(urgencyOf(c(Kind::Added, kNow + 90 * 60000), kNow), ChangeUrgency::Today);
        QCOMPARE(urgencyOf(c(Kind::Moved, kNow + 5 * kHour, kNow + kHour), kNow), ChangeUrgency::Today);
        QCOMPARE(urgencyOf(c(Kind::Added, kNow + 3 * kHour), kNow), ChangeUrgency::Later);
    }
};

QTEST_GUILESS_MAIN(TestSync)
#include "tst_sync.moc"
