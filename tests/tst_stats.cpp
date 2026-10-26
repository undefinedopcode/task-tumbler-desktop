#include "database.h"
#include "operations.h"
#include "stats.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTest>

using namespace tumbler;

namespace {

const QTimeZone kUtc = QTimeZone::UTC;
const QDate kDay(2026, 3, 2);

qint64 at(QDate d, int h, int m = 0)
{
    return QDateTime(d, QTime(h, m), kUtc).toMSecsSinceEpoch();
}

} // namespace

class TestStats : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    Queries q() const { return Queries(m_db->handle()); }
    int m_ids = 0;

    void exec(const QString &sql, const QVariantList &binds)
    {
        QSqlQuery query(m_db->handle());
        query.prepare(sql);
        for (const QVariant &b : binds)
            query.addBindValue(b);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
    }

    void task(const QString &id, int estimate, const QString &status = QStringLiteral("pending"), const QString &category = {})
    {
        Task t;
        t.id = id;
        t.title = QStringLiteral("Task ") + id;
        t.estimateMinutes = estimate;
        t.categoryId = category;
        QString error;
        QVERIFY2(MutationRunner(m_db->handle()).run(ops::createTask(t, at(kDay.addDays(-30), 9)), &error), qPrintable(error));
        exec(QStringLiteral("UPDATE tasks SET status = ? WHERE id = ?"), {status, id});
    }

    void work(const QString &taskId, qint64 start, int minutes)
    {
        exec(QStringLiteral("INSERT INTO work_logs (id, task_id, started_at, ended_at, duration_minutes, created_at) VALUES (?, ?, ?, ?, ?, ?)"),
             {QString::number(++m_ids), taskId, start, start + minutes * 60000LL, minutes, start});
    }

    void history(const QString &taskId, const QString &action, qint64 when, const QVariant &details = {})
    {
        exec(QStringLiteral("INSERT INTO task_history (id, task_id, action, at, details) VALUES (?, ?, ?, ?, ?)"),
             {QString::number(++m_ids), taskId, action, when, details});
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(":memory:") && m_db->migrate());
        m_ids = 0;
    }

    void emptyDatabase()
    {
        QVERIFY(!stats::firstActivity(q(), kUtc));
        const stats::Summary s = stats::summarize(q(), kDay.addDays(-6), kDay, kUtc);
        QCOMPARE(s.days.size(), 7);
        QCOMPARE(s.workedMinutes, 0);
        QCOMPARE(s.overall.tasks, 0);
        QVERIFY(s.tasks.isEmpty());
    }

    void workIsSplitAcrossHoursAndDays()
    {
        task("a", 60);
        work("a", at(kDay, 23, 30), 90); // 23:30 to 01:00
        work("a", at(kDay.addDays(1), 10, 15), 20);
        exec(QStringLiteral("INSERT INTO work_logs (id, task_id, started_at, created_at) VALUES ('open', 'a', ?, ?)"),
             {at(kDay.addDays(1), 11), at(kDay.addDays(1), 11)}); // still running: not counted

        stats::Summary s = stats::summarize(q(), kDay, kDay.addDays(1), kUtc);
        QCOMPARE(s.workedMinutes, 110);
        QCOMPARE(s.sessions, 2);
        QCOMPARE(s.longestSessionMinutes, 90);
        QCOMPARE(s.minutesByHour[23], 30);
        QCOMPARE(s.minutesByHour[0], 60);
        QCOMPARE(s.minutesByHour[10], 20);
        QCOMPARE(s.days[0].workedMinutes, 30);
        QCOMPARE(s.days[1].workedMinutes, 80);
        QCOMPARE(s.tasks.size(), 1);
        QCOMPARE(s.tasks[0].workedMinutes, 110);

        // Only the part inside the range counts, and a session that started
        // before it isn't one of its sessions.
        s = stats::summarize(q(), kDay.addDays(1), kDay.addDays(1), kUtc);
        QCOMPARE(s.workedMinutes, 80);
        QCOMPARE(s.sessions, 1);
        QCOMPARE(s.tasks[0].sessions, 1);

        // Splitting never loses a minute to rounding.
        work("a", at(kDay.addDays(2), 9, 59) + 30000, 3); // 09:59:30 for three minutes
        s = stats::summarize(q(), kDay.addDays(2), kDay.addDays(2), kUtc);
        QCOMPARE(s.workedMinutes, 3);
        QCOMPARE(s.minutesByHour[9] + s.minutesByHour[10], 3);
    }

    void dayBoundariesFollowTheZone()
    {
        task("a", 30);
        work("a", at(kDay, 23, 30), 20); // 23:30 UTC is 08:30 the next day in Tokyo
        const QTimeZone tokyo("Asia/Tokyo");
        const stats::Summary s = stats::summarize(q(), kDay.addDays(1), kDay.addDays(1), tokyo);
        QCOMPARE(s.workedMinutes, 20);
        QCOMPARE(s.minutesByHour[8], 20);
    }

    void finishedTasksAndEstimates()
    {
        exec(QStringLiteral("INSERT INTO categories (id, name, schedule, created_at, updated_at) VALUES ('admin', 'Admin', '{}', 0, 0)"), {});
        task("long", 30, "completed", "admin"); // took twice as long
        task("short", 60, "completed");        // took half
        task("exact", 20, "completed", "admin");
        task("before", 30, "completed");       // finished before the range
        task("open", 30, "in_progress");       // half done: not an estimate result
        task("nohistory", 30, "completed");    // imported without history: uses updated_at
        task("reopened", 30, "pending");

        work("long", at(kDay.addDays(-10), 9), 40); // work before the range still counts towards the task
        work("long", at(kDay, 9), 20);
        work("short", at(kDay, 11), 30);
        work("exact", at(kDay, 13), 20);
        work("before", at(kDay.addDays(-5), 9), 90);
        work("open", at(kDay, 15), 10);

        history("long", "completed", at(kDay, 10));
        history("short", "completed", at(kDay, 12));
        history("exact", "completed", at(kDay.addDays(1), 14));
        history("before", "completed", at(kDay.addDays(-5), 11));
        history("reopened", "completed", at(kDay, 16));
        exec(QStringLiteral("UPDATE tasks SET updated_at = ? WHERE id = 'nohistory'"), {at(kDay, 17)});

        // Routines count as finished, but not as one-off tasks.
        task("routine", 10);
        exec(QStringLiteral("UPDATE tasks SET recurrence = '{\"type\":\"daily\",\"interval\":1}' WHERE id = 'routine'"), {});
        exec(QStringLiteral("INSERT INTO task_occurrences (id, task_id, occurrence_date, status, completed_at, created_at) "
                            "VALUES ('o1', 'routine', '2026-03-02', 'completed', ?, 0)"),
             {at(kDay, 8)});
        history("routine", "completed", at(kDay, 8), QStringLiteral("{\"occurrence_date\":\"2026-03-02\"}"));

        const stats::Summary s = stats::summarize(q(), kDay, kDay.addDays(1), kUtc);
        QCOMPARE(s.finishedTasks, 4); // long, short, exact, nohistory
        QCOMPARE(s.finishedRoutines, 1);
        QCOMPARE(s.days[0].finished, 4);
        QCOMPARE(s.days[1].finished, 1);

        QCOMPARE(s.overall.tasks, 3);
        QCOMPARE(s.overall.ratio, 1.0); // median of 2, 0.5, 1
        QCOMPARE(s.overall.estimatedMinutes, 110);
        QCOMPARE(s.overall.workedMinutes, 110);

        QCOMPARE(s.bySize.size(), 2);
        QCOMPARE(s.bySize[0].label, QStringLiteral("30"));
        QCOMPARE(s.bySize[0].tasks, 2);
        QCOMPARE(s.bySize[0].ratio, 1.5);
        QCOMPARE(s.bySize[1].label, QStringLiteral("60"));
        QCOMPARE(s.bySize[1].ratio, 0.5);

        QCOMPARE(s.byCategory.size(), 1);
        QCOMPARE(s.byCategory[0].label, QStringLiteral("Admin"));
        QCOMPARE(s.byCategory[0].tasks, 2);

        // The breakdown is the range's work, most first.
        QCOMPARE(s.tasks.size(), 4);
        QCOMPARE(s.tasks[0].taskId, QStringLiteral("short"));
        QVERIFY(s.tasks[0].finished);
        QCOMPARE(s.tasks.last().taskId, QStringLiteral("open"));
        QVERIFY(!s.tasks.last().finished);
    }

    void movingTasksAndEnergy()
    {
        task("stuck", 30);
        task("once", 30);
        task("gone", 30, "completed");
        for (int i = 0; i < 3; ++i)
            history("stuck", "bumped", at(kDay, 9 + i));
        history("once", "bumped", at(kDay, 9));
        history("gone", "bumped", at(kDay, 9));
        history("gone", "bumped", at(kDay, 10));
        history("stuck", "bumped", at(kDay.addDays(-3), 9)); // outside the range

        exec(QStringLiteral("INSERT INTO energy_logs (id, logged_at, energy_level) VALUES ('e1', ?, 8), ('e2', ?, 4), ('e3', ?, 3)"),
             {at(kDay, 9, 10), at(kDay.addDays(1), 9, 50), at(kDay, 15)});

        const stats::Summary s = stats::summarize(q(), kDay, kDay.addDays(1), kUtc);
        QCOMPARE(s.moving.size(), 1);
        QCOMPARE(s.moving[0].taskId, QStringLiteral("stuck"));
        QCOMPARE(s.moving[0].bumps, 3);
        QCOMPARE(s.energyByHour[9].value_or(0), 6.0);
        QCOMPARE(s.energyByHour[15].value_or(0), 3.0);
        QVERIFY(!s.energyByHour[12]);

        QCOMPARE(stats::firstActivity(q(), kUtc), std::optional<QDate>(kDay.addDays(-3)));
    }
};

QTEST_GUILESS_MAIN(TestStats)
#include "tst_stats.moc"
