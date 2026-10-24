#include "database.h"
#include "operations.h"
#include "queries.h"

#include <QSqlQuery>
#include <QTest>

using namespace tumbler;

namespace {

constexpr qint64 kNow = 1'770'000'000'000; // fixed clock

QVariant scalar(QSqlDatabase db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql) || !q.next())
        return {};
    return q.value(0);
}

Task makeTask(const QString &id, const QString &title, const QString &parent = {})
{
    Task t;
    t.id = id;
    t.title = title;
    t.parentId = parent;
    return t;
}

} // namespace

class TestOperations : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    QSqlDatabase db() const { return m_db->handle(); }

    AppliedMutation run(const Mutation &m)
    {
        MutationRunner runner(db());
        QString error;
        auto applied = runner.run(m, &error);
        if (!applied)
            qFatal("mutation failed: %s", qPrintable(error));
        return *applied;
    }

    AppliedMutation run(const std::optional<Mutation> &m)
    {
        if (!m)
            qFatal("builder refused");
        return run(*m);
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(QStringLiteral(":memory:")) && m_db->migrate());
    }

    void createUndoRedo()
    {
        MutationRunner runner(db());
        auto applied = run(ops::createTask(makeTask("t1", "Write report"), kNow));
        QCOMPARE(scalar(db(), "SELECT title FROM tasks").toString(), QStringLiteral("Write report"));
        QVERIFY(applied.tables().contains("tasks"));

        QVERIFY(runner.revert(applied));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM tasks").toInt(), 0);
        QVERIFY(runner.reapply(applied));
        QCOMPARE(scalar(db(), "SELECT created_at FROM tasks").toLongLong(), kNow);
    }

    void updateValidatesAndUndoes()
    {
        run(ops::createTask(makeTask("t1", "A"), kNow));
        run(ops::createTask(makeTask("t2", "B", "t1"), kNow));
        Queries q(db());
        QString error;

        QVERIFY(!ops::updateTask(q, "t1", {{"title", "  "}}, kNow, &error));
        QVERIFY(!ops::updateTask(q, "t1", {{"parent_task_id", "t2"}}, kNow, &error)); // loop
        QVERIFY(error.contains("ancestor"));
        QVERIFY(!ops::updateTask(q, "t1", {{"status", "completed"}}, kNow, &error));

        auto applied = run(ops::updateTask(q, "t1",
                                           {{"title", "A2"},
                                            {"priority", "urgent"},
                                            {"tags", QStringList{"x", " x ", "y"}},
                                            {"due_date", "2026-03-01"},
                                            {"recurrence", QVariantMap{{"type", "daily"}, {"interval", 2}}}},
                                           kNow + 1));
        const auto t = q.task("t1");
        QCOMPARE(t->title, QStringLiteral("A2"));
        QCOMPARE(t->priority, Priority::Urgent);
        QCOMPARE(t->tags, QStringList({"x", "y"}));
        QVERIFY(t->recurrence && t->recurrence->interval == 2 && t->recurrence->startDate.isValid());

        MutationRunner(db()).revert(applied);
        QCOMPARE(q.task("t1")->title, QStringLiteral("A"));
        QVERIFY(!q.task("t1")->recurrence);
    }

    void completingLastSubtaskCompletesParent()
    {
        run(ops::createTask(makeTask("p", "Trip"), kNow));
        run(ops::createTask(makeTask("c1", "Book", "p"), kNow));
        run(ops::createTask(makeTask("c2", "Pack", "p"), kNow));
        Queries q(db());

        run(ops::setTaskStatus(q, "c1", TaskStatus::Completed, kNow));
        QCOMPARE(q.task("p")->status, TaskStatus::Pending);

        auto applied = run(ops::setTaskStatus(q, "c2", TaskStatus::Cancelled, kNow));
        QCOMPARE(q.task("p")->status, TaskStatus::Pending); // cancelling isn't completing

        MutationRunner(db()).revert(applied);
        auto done = run(ops::setTaskStatus(q, "c2", TaskStatus::Completed, kNow));
        QCOMPARE(q.task("p")->status, TaskStatus::Completed);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_history WHERE task_id = 'p' AND action = 'completed'").toInt(), 1);

        // One undo reverts both the subtask and the parent.
        MutationRunner(db()).revert(done);
        QCOMPARE(q.task("p")->status, TaskStatus::Pending);
        QCOMPARE(q.task("c2")->status, TaskStatus::Pending);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_history WHERE task_id = 'p'").toInt(), 0);
    }

    // Undoing a status change must not delete and re-insert the task row,
    // which would cascade away its occurrences and schedules.
    void undoNeverCascades()
    {
        run(ops::createTask(makeTask("t1", "A"), kNow));
        run(ops::scheduleTask("s1", "t1", kNow, kNow + 3600000, kNow));
        QSqlQuery(db()).exec("INSERT INTO task_occurrences (id, task_id, occurrence_date, created_at) VALUES ('o1', 't1', '2026-03-01', 0)");
        Queries q(db());

        auto applied = run(ops::setTaskStatus(q, "t1", TaskStatus::Completed, kNow));
        QVERIFY(MutationRunner(db()).revert(applied));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM schedules").toInt(), 1);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_occurrences").toInt(), 1);
    }

    void deleteRestoresEverything()
    {
        run(ops::createTask(makeTask("p", "Parent"), kNow));
        run(ops::createTask(makeTask("c", "Child", "p"), kNow));
        run(ops::createTask(makeTask("other", "Other"), kNow));
        Queries q(db());
        run(ops::addDependency(q, "other", "p", kNow));
        run(ops::scheduleTask("s1", "p", kNow, kNow + 1000, kNow));
        run(ops::startWork(q, "w1", "p", kNow));
        run(ops::stopWork(q, kNow + 120000));

        auto applied = run(ops::deleteTask("p"));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM tasks").toInt(), 2);
        QVERIFY(scalar(db(), "SELECT parent_task_id FROM tasks WHERE id = 'c'").isNull());
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_dependencies").toInt(), 0);

        QString error;
        QVERIFY2(MutationRunner(db()).revert(applied, &error), qPrintable(error));
        QCOMPARE(scalar(db(), "SELECT parent_task_id FROM tasks WHERE id = 'c'").toString(), QStringLiteral("p"));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_dependencies").toInt(), 1);
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM schedules").toInt(), 1);
        QCOMPARE(scalar(db(), "SELECT duration_minutes FROM work_logs").toInt(), 2);
        QCOMPARE(scalar(db(), "PRAGMA foreign_key_check").toString(), QString());
    }

    void dependencyRules()
    {
        run(ops::createTask(makeTask("a", "A"), kNow));
        run(ops::createTask(makeTask("b", "B"), kNow));
        Queries q(db());
        QString error;
        run(ops::addDependency(q, "b", "a", kNow));
        QVERIFY(!ops::addDependency(q, "a", "b", kNow, &error));
        QVERIFY(error.contains("loop"));
        QVERIFY(!ops::addDependency(q, "b", "a", kNow, &error));
        QVERIFY(!ops::addDependency(q, "a", "a", kNow, &error));
        run(ops::removeDependency("b", "a"));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_dependencies").toInt(), 0);
    }

    void workSessions()
    {
        run(ops::createTask(makeTask("a", "A"), kNow));
        run(ops::createTask(makeTask("b", "B"), kNow));
        Queries q(db());

        run(ops::startWork(q, "w1", "a", kNow));
        QCOMPARE(q.task("a")->status, TaskStatus::InProgress);
        // Starting another ends the first.
        run(ops::startWork(q, "w2", "b", kNow + 25 * 60000));
        QCOMPARE(q.activeWorkLog()->taskId, QStringLiteral("b"));
        QCOMPARE(scalar(db(), "SELECT duration_minutes FROM work_logs WHERE id = 'w1'").toInt(), 25);

        // Completing the task ends its session.
        run(ops::setTaskStatus(q, "b", TaskStatus::Completed, kNow + 30 * 60000));
        QVERIFY(!q.activeWorkLog());
        QCOMPARE(q.loggedMinutes().value("b"), 5);

        QString error;
        QVERIFY(!ops::stopWork(q, kNow, &error));
    }

    void schedules()
    {
        run(ops::createTask(makeTask("a", "A"), kNow));
        Queries q(db());
        run(ops::scheduleTask("s1", "a", kNow, kNow + 1800000, kNow));

        run(ops::moveSchedule(q, "s1", kNow + 900000, kNow + 2700000, true, kNow));
        QCOMPARE(q.schedule("s1")->bumpedCount, 1);
        QCOMPARE(scalar(db(), "SELECT action FROM task_history WHERE task_id = 'a'").toString(), QStringLiteral("bumped"));
        run(ops::moveSchedule(q, "s1", kNow, kNow + 1800000, false, kNow));
        QCOMPARE(q.schedule("s1")->bumpedCount, 1);

        auto applied = run(ops::completeSchedule(q, "s1", kNow + 1800000));
        QVERIFY(q.schedule("s1")->completed);
        QCOMPARE(q.task("a")->status, TaskStatus::Completed);
        MutationRunner(db()).revert(applied);
        QVERIFY(!q.schedule("s1")->completed);
        QCOMPARE(q.task("a")->status, TaskStatus::Pending);

        run(ops::deleteSchedule("s1"));
        QVERIFY(!q.schedule("s1"));
    }

    void occurrencesMaterialiseOnce()
    {
        Task t = makeTask("r", "Meds");
        RecurrencePattern p;
        p.startDate = QDate(2026, 3, 1);
        t.recurrence = p;
        run(ops::createTask(t, kNow));
        Queries q(db());

        QCOMPARE(materializeOccurrences(db(), q.tasks(), QDate(2026, 3, 1), QDate(2026, 3, 7), kNow), 7);
        QCOMPARE(materializeOccurrences(db(), q.tasks(), QDate(2026, 3, 1), QDate(2026, 3, 8), kNow), 1);

        const auto occ = q.occurrences(QDate(2026, 3, 2), QDate(2026, 3, 2));
        QCOMPARE(occ.size(), 1);
        run(ops::setOccurrenceStatus(q, occ.first().id, OccurrenceStatus::Completed, kNow));
        QCOMPARE(q.occurrence(occ.first().id)->status, OccurrenceStatus::Completed);
        run(ops::setOccurrenceTime(occ.first().id, QTime(7, 30)));
        QCOMPARE(q.occurrence(occ.first().id)->scheduledTime, QTime(7, 30));

        // Finished recurring tasks stop generating.
        run(ops::setTaskStatus(q, "r", TaskStatus::Completed, kNow));
        QCOMPARE(materializeOccurrences(db(), q.tasks(), QDate(2026, 3, 1), QDate(2026, 3, 31), kNow), 0);
    }

    void categories()
    {
        Category def;
        def.id = "def";
        def.name = "Default";
        def.isDefault = true;
        run(ops::saveCategory(def, true, kNow));
        Category work;
        work.id = "work";
        work.name = "Work";
        work.schedule = CategorySchedule::weekdays(QTime(9, 0), QTime(17, 0));
        run(ops::saveCategory(work, true, kNow));

        Task t = makeTask("a", "A");
        t.categoryId = "work";
        run(ops::createTask(t, kNow));
        Queries q(db());
        QString error;
        QVERIFY(!ops::deleteCategory(q, "def", kNow, &error));

        // Making Work the default clears Default's flag; undo restores it.
        work.isDefault = true;
        auto applied = run(ops::saveCategory(work, false, kNow));
        QCOMPARE(q.defaultCategory()->id, QStringLiteral("work"));
        MutationRunner(db()).revert(applied);
        QCOMPARE(q.defaultCategory()->id, QStringLiteral("def"));

        run(ops::deleteCategory(q, "work", kNow));
        QCOMPARE(q.task("a")->categoryId, QStringLiteral("def"));
    }

    void eventsSaveEditDelete()
    {
        Queries q(db());
        Event e;
        e.id = "e1";
        e.title = "Physio";
        e.startAt = kNow;
        e.endAt = kNow + 3600000;
        e.blocking.mode = BlockingMode::TagsOnly;
        e.blocking.allowedTags = {"phone", " "};
        run(ops::saveEvent(e, true, "America/Vancouver", kNow));
        QCOMPARE(q.event("e1")->blocking.allowedTags, QStringList({"phone"}));
        QCOMPARE(scalar(db(), "SELECT timezone FROM events").toString(), QStringLiteral("America/Vancouver"));

        e.allDay = true;
        e.startDate = QDate(2026, 3, 5);
        e.endDate = QDate(2026, 3, 6);
        auto applied = run(ops::saveEvent(e, false, "America/Vancouver", kNow));
        QVERIFY(q.event("e1")->allDay);
        QVERIFY(scalar(db(), "SELECT start_at FROM events").isNull());
        MutationRunner(db()).revert(applied);
        QVERIFY(!q.event("e1")->allDay);

        QString error;
        e.endDate = QDate(2026, 3, 4);
        QVERIFY(!ops::saveEvent(e, false, {}, kNow, &error));
        e.title = " ";
        QVERIFY(!ops::saveEvent(e, false, {}, kNow, &error));

        run(ops::deleteEvent("e1"));
        QVERIFY(!q.event("e1"));
    }

    void energyAndHealth()
    {
        Queries q(db());
        QString error;
        QVERIFY(!ops::logEnergy("x", 11, kNow, {}, &error));
        run(ops::logEnergy("en1", 4, kNow - 7200000, "meh", nullptr));
        run(ops::logEnergy("en2", 7, kNow - 3600000, {}, nullptr));
        QCOMPARE(q.latestEnergy(kNow - 86400000)->level, 7);
        QVERIFY(!q.latestEnergy(kNow));
        QCOMPARE(q.energyLogs(kNow - 86400000, kNow).size(), 2);

        HealthLog h;
        h.date = QDate(2026, 3, 5);
        h.sleepScore = 62;
        h.hoursSlept = 6.5;
        run(ops::saveHealth(q, h, kNow));
        h.sleepScore = 70; // same day replaces
        auto applied = run(ops::saveHealth(q, h, kNow));
        QCOMPARE(q.healthLogs(QDate(2026, 3, 1), QDate(2026, 3, 31)).size(), 1);
        QCOMPARE(*q.healthLog(QDate(2026, 3, 5))->sleepScore, 70);
        MutationRunner(db()).revert(applied);
        QCOMPARE(*q.healthLog(QDate(2026, 3, 5))->sleepScore, 62);
        h.readinessScore = 101;
        QVERIFY(!ops::saveHealth(q, h, kNow, &error));
    }

    void launchersAndTrimmedStop()
    {
        run(ops::createTask(makeTask("t", "Write"), kNow));
        Queries q(db());
        QString error;
        TaskAction a;
        a.id = "a1";
        a.taskId = "t";
        a.kind = TaskAction::Kind::Url;
        a.target = " https://example.com/doc ";
        run(ops::addAction(q, a, kNow));
        a.id = "a2";
        a.kind = TaskAction::Kind::Command;
        a.target = R"(["kate", "/home/me/report.md"])";
        a.onStart = false;
        run(ops::addAction(q, a, kNow));
        const auto actions = q.actions("t");
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions[0].target, QStringLiteral("https://example.com/doc"));
        QCOMPARE(actions[1].sortOrder, 1);
        QVERIFY(!actions[1].onStart);

        a.id = "bad";
        a.target = "kate /home/me/report.md"; // a shell string, not argv
        QVERIFY(!ops::addAction(q, a, kNow, &error));
        a.target = "[]";
        QVERIFY(!ops::addAction(q, a, kNow, &error));
        a.taskId = "missing";
        QVERIFY(!ops::addAction(q, a, kNow, &error));

        // Deleting the task takes its launchers with it, and undo brings them back.
        auto deleted = run(ops::deleteTask("t"));
        QCOMPARE(scalar(db(), "SELECT COUNT(*) FROM task_actions").toInt(), 0);
        MutationRunner(db()).revert(deleted);
        QCOMPARE(q.actions("t").size(), 2);
        run(ops::removeAction("a1"));
        QCOMPARE(q.actions("t").size(), 1);

        // Stopping as of an earlier moment leaves the idle time out.
        run(ops::startWork(q, "w", "t", kNow));
        run(ops::stopWorkAt(q, kNow + 20 * 60000, kNow + 60 * 60000));
        QCOMPARE(scalar(db(), "SELECT duration_minutes FROM work_logs").toInt(), 20);
        QVERIFY(!ops::stopWorkAt(q, kNow, kNow, &error));
    }

    void settings()
    {
        Queries q(db());
        run(ops::setSetting("reminder_minutes", 10));
        run(ops::setSetting("reminder_minutes", 5));
        QCOMPARE(q.setting("reminder_minutes").toInt(), 5);
        QCOMPARE(q.setting("missing", "x").toString(), QStringLiteral("x"));
    }
};

QTEST_GUILESS_MAIN(TestOperations)
#include "tst_operations.moc"
