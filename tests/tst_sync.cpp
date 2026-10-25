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
    }
};

QTEST_GUILESS_MAIN(TestSync)
#include "tst_sync.moc"
