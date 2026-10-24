#include "database.h"
#include "operations.h"
#include "planner.h"
#include "today.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTest>

using namespace tumbler;

namespace {
const QTimeZone kZone("America/Vancouver");
constexpr qint64 kMin = 60 * 1000;

qint64 at(int month, int day, int h, int m = 0)
{
    return QDateTime(QDate(2026, month, day), QTime(h, m), kZone).toMSecsSinceEpoch();
}

QString hm(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, kZone).toString(QStringLiteral("MM-dd HH:mm"));
}
} // namespace

class TestPlanner : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;

    void run(const Mutation &m)
    {
        QString error;
        if (!MutationRunner(m_db->handle()).run(m, &error))
            qFatal("%s", qPrintable(error));
    }
    void run(const std::optional<Mutation> &m) { run(*m); }

    Task addTask(const QString &id, int minutes = 30, const QString &category = "work")
    {
        Task t;
        t.id = id;
        t.title = id;
        t.estimateMinutes = minutes;
        t.categoryId = category;
        run(ops::createTask(t, 0));
        return t;
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(":memory:") && m_db->migrate());
        Category work;
        work.id = "work";
        work.name = "Work";
        work.isDefault = true;
        work.schedule = CategorySchedule::weekdays(QTime(9, 0), QTime(17, 0));
        run(ops::saveCategory(work, true, 0));
    }

    void findSlotBasics()
    {
        const QList<Interval> windows{{0, 100 * kMin}};
        QCOMPARE(findSlot(windows, {}, 0, 30 * kMin), std::optional<qint64>(0));
        QCOMPARE(findSlot(windows, {{0, 20 * kMin}}, 0, 30 * kMin), std::optional<qint64>(20 * kMin));
        // Gap too small between blocks: skip past both.
        QCOMPARE(findSlot(windows, {{0, 20 * kMin}, {40 * kMin, 60 * kMin}}, 0, 30 * kMin), std::optional<qint64>(60 * kMin));
        // Snaps up to the 5-minute grid.
        QCOMPARE(findSlot(windows, {}, 7 * kMin, 10 * kMin), std::optional<qint64>(10 * kMin));
        QVERIFY(!findSlot(windows, {{0, 80 * kMin}}, 0, 30 * kMin));
        QVERIFY(!findSlot(windows, {}, 0, 0));
        QCOMPARE(mergeIntervals({{5, 10}, {0, 5}, {20, 30}, {25, 26}}), QList<Interval>({{0, 10}, {20, 30}}));
    }

    void nextFreeSlotRespectsHoursBlocksAndEvents()
    {
        const Task t = addTask("report", 60);
        Queries q(m_db->handle());

        // Thursday 2026-03-05, 10:07: next grid slot inside work hours.
        auto slot = nextFreeSlot(q, t, at(3, 5, 10, 7), kZone);
        QVERIFY(slot);
        QCOMPARE(hm(slot->start), QStringLiteral("03-05 10:10"));

        // A block 10:00-12:00 pushes it to 12:00.
        run(ops::scheduleTask("s1", "report", at(3, 5, 10), at(3, 5, 12), 0));
        addTask("other");
        run(ops::scheduleTask("s2", "other", at(3, 5, 10), at(3, 5, 12), 0));
        slot = nextFreeSlot(q, t, at(3, 5, 10, 7), kZone, 14, "s1");
        QCOMPARE(hm(slot->start), QStringLiteral("03-05 12:00"));

        // A blocking event 12:00-16:30 leaves no hour today: Friday 09:00.
        QSqlQuery(m_db->handle()).exec(QStringLiteral(
            "INSERT INTO events (id, title, is_all_day, start_at, end_at, blocking_mode, created_at, updated_at) "
            "VALUES ('e1', 'Offsite', 0, %1, %2, 'block_all', 0, 0)").arg(at(3, 5, 12)).arg(at(3, 5, 16, 30)));
        slot = nextFreeSlot(q, t, at(3, 5, 10, 7), kZone, 14, "s1");
        QCOMPARE(hm(slot->start), QStringLiteral("03-06 09:00"));

        // An all-day block_all event on Friday pushes to Monday (weekend off).
        QSqlQuery(m_db->handle()).exec(
            "INSERT INTO events (id, title, is_all_day, start_date, end_date, blocking_mode, created_at, updated_at) "
            "VALUES ('e2', 'Holiday', 1, '2026-03-06', '2026-03-06', 'block_all', 0, 0)");
        slot = nextFreeSlot(q, t, at(3, 5, 10, 7), kZone, 14, "s1");
        QCOMPARE(hm(slot->start), QStringLiteral("03-09 09:00"));
    }

    void tagsOnlyEventsLetMatchingTasksThrough()
    {
        Task phone = addTask("call", 15);
        run(*ops::updateTask(Queries(m_db->handle()), "call", {{"tags", QStringList{"phone"}}}, 0));
        phone.tags = {"phone"};
        QSqlQuery(m_db->handle()).exec(QStringLiteral(
            "INSERT INTO events (id, title, is_all_day, start_at, end_at, blocking_mode, allowed_tags, created_at, updated_at) "
            "VALUES ('e1', 'Commute', 0, %1, %2, 'tags_only', '[\"phone\"]', 0, 0)").arg(at(3, 5, 9)).arg(at(3, 5, 10)));
        Queries q(m_db->handle());
        QCOMPARE(hm(nextFreeSlot(q, phone, at(3, 5, 9), kZone)->start), QStringLiteral("03-05 09:00"));
        QCOMPARE(hm(nextFreeSlot(q, addTask("deep", 15), at(3, 5, 9), kZone)->start), QStringLiteral("03-05 10:00"));
    }

    void timedRecurringItemsAreBusy()
    {
        Task meds;
        meds.id = "meds";
        meds.title = "Meds";
        meds.estimateMinutes = 15;
        meds.scheduledTime = QTime(9, 0);
        RecurrencePattern p;
        p.startDate = QDate(2026, 3, 1);
        meds.recurrence = p;
        run(ops::createTask(meds, 0));
        Queries q(m_db->handle());
        materializeOccurrences(m_db->handle(), q.tasks(), QDate(2026, 3, 1), QDate(2026, 3, 31), 0);

        QCOMPARE(hm(nextFreeSlot(q, addTask("x", 30), at(3, 5, 9), kZone)->start), QStringLiteral("03-05 09:15"));
    }

    void todaySections()
    {
        addTask("past");
        addTask("current");
        addTask("later");
        addTask("ready-high");
        addTask("ready-low");
        addTask("blocked");
        addTask("parent");
        addTask("child");
        Queries q(m_db->handle());
        run(ops::updateTask(q, "ready-high", {{"priority", "high"}}, 0));
        run(ops::updateTask(q, "ready-low", {{"priority", "low"}}, 0));
        run(ops::updateTask(q, "child", {{"parent_task_id", "parent"}}, 0));
        run(ops::addDependency(q, "blocked", "ready-low", 0));

        run(ops::scheduleTask("s-past", "past", at(3, 5, 9), at(3, 5, 10), 0));
        run(ops::scheduleTask("s-cur", "current", at(3, 5, 11), at(3, 5, 12), 0));
        run(ops::scheduleTask("s-later", "later", at(3, 5, 12, 10), at(3, 5, 13), 0));
        run(ops::startWork(q, "w", "current", at(3, 5, 11)));

        Task timeless;
        timeless.id = "water";
        timeless.title = "Water plants";
        RecurrencePattern p;
        p.startDate = QDate(2026, 3, 1);
        timeless.recurrence = p;
        run(ops::createTask(timeless, 0));
        materializeOccurrences(m_db->handle(), q.tasks(), QDate(2026, 3, 5), QDate(2026, 3, 5), 0);

        const TodayView v = buildToday(q, at(3, 5, 11, 55), kZone);
        QCOMPARE(v.date, QDate(2026, 3, 5));
        QCOMPARE(v.workingTitle, QStringLiteral("current"));
        QCOMPARE(v.now.size(), 1);
        QVERIFY(v.now.first().working);
        QCOMPARE(v.overdue.size(), 1);
        QCOMPARE(v.overdue.first().id, QStringLiteral("s-past"));
        // Upcoming: the timeless recurring item (never overdue) and the 12:10 block.
        QCOMPARE(v.upcoming.size(), 2);
        QVERIFY(v.upcoming[0].timeless);
        QCOMPARE(v.upcoming[1].id, QStringLiteral("s-later"));
        QVERIFY(v.upcoming[1].startsSoon);

        QStringList ready;
        for (const ReadyTask &r : v.ready)
            ready.append(r.task.id);
        // Not: scheduled tasks, the blocked task, the parent with an open child.
        QCOMPARE(ready, QStringList({"ready-high", "child", "ready-low"}));
    }
};

QTEST_GUILESS_MAIN(TestPlanner)
#include "tst_planner.moc"
