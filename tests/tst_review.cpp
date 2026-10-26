#include "database.h"
#include "operations.h"
#include "review.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

using namespace tumbler;

namespace {

const QTimeZone kUtc = QTimeZone::UTC;
const QDate kDay(2026, 3, 4); // a Wednesday

qint64 at(QDate d, int h, int m = 0)
{
    return QDateTime(d, QTime(h, m), kUtc).toMSecsSinceEpoch();
}

} // namespace

class TestReview : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    Queries q() const { return Queries(m_db->handle()); }
    int m_ids = 0;

    void exec(const QString &sql, const QVariantList &binds = {})
    {
        QSqlQuery query(m_db->handle());
        query.prepare(sql);
        for (const QVariant &b : binds)
            query.addBindValue(b);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
    }

    void run(const std::optional<Mutation> &m)
    {
        QVERIFY(m);
        QString error;
        QVERIFY2(MutationRunner(m_db->handle()).run(*m, &error), qPrintable(error));
    }

    // Created a month ago; `touched` sets when it was last changed.
    void task(const QString &id, const QString &status = QStringLiteral("pending"), qint64 touched = 0)
    {
        Task t;
        t.id = id;
        t.title = QStringLiteral("Task ") + id;
        t.estimateMinutes = 30;
        run(ops::createTask(t, at(kDay.addDays(-30), 9)));
        exec(QStringLiteral("UPDATE tasks SET status = ?, updated_at = ? WHERE id = ?"),
             {status, touched ? touched : at(kDay.addDays(-1), 9), id});
    }

    void block(const QString &id, const QString &taskId, qint64 start, bool completed = false)
    {
        run(ops::scheduleTask(id, taskId, start, start + 30 * 60000, 0));
        if (completed)
            exec(QStringLiteral("UPDATE schedules SET completed = 1 WHERE id = ?"), {id});
    }

    void work(const QString &taskId, qint64 start, int minutes)
    {
        exec(QStringLiteral("INSERT INTO work_logs (id, task_id, started_at, ended_at, duration_minutes, created_at) VALUES (?, ?, ?, ?, ?, ?)"),
             {QString::number(++m_ids), taskId, start, start + minutes * 60000LL, minutes, start});
    }

    void completed(const QString &taskId, qint64 when)
    {
        exec(QStringLiteral("UPDATE tasks SET status = 'completed' WHERE id = ?"), {taskId});
        exec(QStringLiteral("INSERT INTO task_history (id, task_id, action, at) VALUES (?, ?, 'completed', ?)"),
             {QString::number(++m_ids), taskId, when});
    }

    void routine(const QString &id, const QString &occurrenceStatus, QDate date, const QString &time = {})
    {
        Task t;
        t.id = id;
        t.title = QStringLiteral("Routine ") + id;
        t.recurrence = RecurrencePattern{};
        if (!time.isEmpty())
            t.scheduledTime = QTime::fromString(time, QStringLiteral("HH:mm"));
        run(ops::createTask(t, 0));
        exec(QStringLiteral("INSERT INTO task_occurrences (id, task_id, occurrence_date, status, completed_at, created_at) "
                            "VALUES (?, ?, ?, ?, ?, 0)"),
             {id + QStringLiteral("-occ"), id, date.toString(Qt::ISODate), occurrenceStatus,
              occurrenceStatus == QLatin1String("completed") ? QVariant(at(date, 8)) : QVariant()});
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(":memory:") && m_db->migrate());
        m_ids = 0;
    }

    void emptyDay()
    {
        const review::DayReview r = review::reviewDay(q(), kDay, kUtc);
        QCOMPARE(r.date, kDay);
        QVERIFY(r.wins.isEmpty());
        QVERIFY(r.leftovers.isEmpty());
        QCOMPARE(r.tomorrow.date, kDay.addDays(1));
        QCOMPARE(r.tomorrow.blocks, 0);
    }

    void winsAndProgress()
    {
        task("done");
        work("done", at(kDay.addDays(-2), 10), 25); // earlier work counts towards the task
        work("done", at(kDay, 10), 20);
        completed("done", at(kDay, 11));
        task("started", "in_progress");
        work("started", at(kDay, 14), 40);
        task("yesterday");
        completed("yesterday", at(kDay.addDays(-1), 15));
        routine("walk", "completed", kDay);

        const review::DayReview r = review::reviewDay(q(), kDay, kUtc);
        QCOMPARE(r.workedMinutes, 60);
        QCOMPARE(r.wins.size(), 2);
        QCOMPARE(r.wins[0].title, QStringLiteral("Routine walk")); // 08:00
        QVERIFY(r.wins[0].routine);
        QCOMPARE(r.wins[1].taskId, QStringLiteral("done"));
        QCOMPARE(r.wins[1].workedMinutes, 45);
        QCOMPARE(r.progress.size(), 1);
        QCOMPARE(r.progress[0].taskId, QStringLiteral("started"));
        QCOMPARE(r.progress[0].minutes, 40);
    }

    void leftovers()
    {
        task("missed");
        block("missed-b", "missed", at(kDay, 9));
        task("later");
        block("later-b", "later", at(kDay, 20)); // still to come today: a leftover all the same
        task("old");
        block("old-b", "old", at(kDay.addDays(-3), 9));
        task("ancient");
        block("ancient-b", "ancient", at(kDay.addDays(-20), 9)); // past the lookback
        task("finished");
        block("finished-b", "finished", at(kDay, 10)); // block not ticked, task done elsewhere
        completed("finished", at(kDay, 10, 30));
        task("ticked");
        block("ticked-b", "ticked", at(kDay, 11), true);
        task("tomorrow");
        block("tomorrow-b", "tomorrow", at(kDay.addDays(1), 9));
        routine("meds", "pending", kDay, "08:00");
        routine("stretch", "pending", kDay); // no time of day
        routine("skipped", "skipped", kDay);

        const review::DayReview r = review::reviewDay(q(), kDay, kUtc);
        QStringList ids;
        for (const review::Leftover &l : r.leftovers)
            ids.append(l.id);
        QCOMPARE(ids, QStringList({"old-b", "meds-occ", "missed-b", "later-b", "stretch-occ"}));
        QVERIFY(r.leftovers[0].earlier);
        QVERIFY(!r.leftovers[2].earlier);
        QCOMPARE(r.leftovers[1].kind, review::Leftover::Kind::Routine);
        QVERIFY(r.leftovers[4].timeless);
    }

    void tomorrowAtAGlance()
    {
        task("a");
        block("a-b", "a", at(kDay.addDays(1), 10));
        task("b");
        block("b-b", "b", at(kDay.addDays(1), 14));
        routine("meds", "pending", kDay.addDays(1), "08:30");
        Event e;
        e.id = "e";
        e.title = "Dentist";
        e.startAt = at(kDay.addDays(1), 9);
        e.endAt = at(kDay.addDays(1), 10);
        run(ops::saveEvent(e, true, "UTC", 0));

        const review::Tomorrow t = review::reviewDay(q(), kDay, kUtc).tomorrow;
        QCOMPARE(t.blocks, 2);
        QCOMPARE(t.routines, 1);
        QCOMPARE(t.events, 1);
        QCOMPARE(t.firstTitle, QStringLiteral("Routine meds"));
        QCOMPARE(t.firstAt, at(kDay.addDays(1), 8, 30));
    }

    void weekStaleAndDueSoon()
    {
        const QDate from = kDay.addDays(-6);
        task("old", "pending", at(kDay.addDays(-40), 9));
        task("older", "pending", at(kDay.addDays(-60), 9));
        task("recent", "pending", at(kDay.addDays(-5), 9));
        task("worked", "pending", at(kDay.addDays(-60), 9));
        work("worked", at(kDay.addDays(-2), 9), 10); // touched by working on it
        task("planned", "pending", at(kDay.addDays(-60), 9));
        block("planned-b", "planned", at(kDay.addDays(2), 9));
        task("parent", "pending", at(kDay.addDays(-60), 9));
        task("step", "pending");
        exec(QStringLiteral("UPDATE tasks SET parent_task_id = 'parent' WHERE id = 'step'"));

        task("due", "pending");
        exec(QStringLiteral("UPDATE tasks SET due_date = ? WHERE id = 'due'"), {kDay.addDays(3).toString(Qt::ISODate)});
        task("overdue", "pending");
        exec(QStringLiteral("UPDATE tasks SET due_date = ? WHERE id = 'overdue'"), {kDay.addDays(-1).toString(Qt::ISODate)});
        task("far", "pending");
        exec(QStringLiteral("UPDATE tasks SET due_date = ? WHERE id = 'far'"), {kDay.addDays(20).toString(Qt::ISODate)});
        task("dueplanned", "pending");
        exec(QStringLiteral("UPDATE tasks SET due_date = ? WHERE id = 'dueplanned'"), {kDay.addDays(2).toString(Qt::ISODate)});
        block("dueplanned-b", "dueplanned", at(kDay.addDays(1), 9));

        const review::WeekReview w = review::reviewWeek(q(), from, kDay, kUtc);
        QStringList stale;
        for (const review::Stale &s : w.stale)
            stale.append(s.taskId);
        QCOMPARE(stale, QStringList({"older", "old"}));
        QCOMPARE(w.stale[0].days, 60);
        QStringList due;
        for (const review::DueSoon &d : w.dueSoon)
            due.append(d.taskId);
        QCOMPARE(due, QStringList({"overdue", "due"}));
    }
};

QTEST_GUILESS_MAIN(TestReview)
#include "tst_review.moc"
