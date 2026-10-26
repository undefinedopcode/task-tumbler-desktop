#include "database.h"
#include "dayplanner.h"
#include "operations.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTest>

using namespace tumbler;

namespace {
const QTimeZone kZone("America/Vancouver");
const QDate kDay(2026, 3, 5); // Thursday

qint64 at(int h, int m = 0)
{
    return QDateTime(kDay, QTime(h, m), kZone).toMSecsSinceEpoch();
}

QString hm(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms, kZone).toString(QStringLiteral("HH:mm"));
}

QStringList layout(const DayPlan &p)
{
    QStringList out;
    for (const PlannedBlock &b : p.blocks)
        out.append(QStringLiteral("%1-%2 %3").arg(hm(b.start), hm(b.end), b.taskId));
    return out;
}

QStringList unplaced(const DayPlan &p)
{
    QStringList out;
    for (const UnplacedTask &u : p.unplaced)
        out.append(u.taskId);
    out.sort();
    return out;
}
} // namespace

class TestDayPlanner : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;

    void run(const std::optional<Mutation> &m)
    {
        QString error;
        if (!m || !MutationRunner(m_db->handle()).run(*m, &error))
            qFatal("mutation failed: %s", qPrintable(error));
    }

    void add(const QString &id, int minutes, Priority p = Priority::Medium, Energy e = Energy::Medium, const QString &cat = "work")
    {
        Task t;
        t.id = id;
        t.title = id;
        t.estimateMinutes = minutes;
        t.priority = p;
        t.energy = e;
        t.categoryId = cat;
        run(ops::createTask(t, ++m_created)); // creation order breaks ties
    }

    qint64 m_created = 0;

    PlanOptions options(int fromHour = 9)
    {
        PlanOptions o;
        o.day = kDay;
        o.earliest = at(fromHour);
        o.breakMinutes = 0;
        return o;
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
        work.schedule = CategorySchedule::weekdays(QTime(9, 0), QTime(12, 0));
        run(ops::saveCategory(work, true, 0));
        Category home;
        home.id = "home";
        home.name = "Home";
        home.schedule = CategorySchedule::everyDay(QTime(18, 0), QTime(20, 0));
        run(ops::saveCategory(home, true, 0));
    }

    void ranksAndPacksWithinCategoryHours()
    {
        add("low", 60, Priority::Low);
        add("urgent", 60, Priority::Urgent);
        add("medium", 60);
        add("dishes", 30, Priority::Medium, Energy::Low, "home");
        add("toolong", 120); // no room left once the others are in
        const DayPlan p = planDay(Queries(m_db->handle()), options(), kZone);
        QCOMPARE(layout(p), QStringList({"09:00-10:00 urgent", "10:00-11:00 medium", "11:00-12:00 low", "18:00-18:30 dishes"}));
        QCOMPARE(unplaced(p), QStringList({"toolong"}));
        QVERIFY(p.unplaced.first().reason.contains("No room"));
        QVERIFY(p.blocks.first().reason.contains("Urgent"));
    }

    void breaksBetweenBlocks()
    {
        add("a", 50, Priority::High);
        add("b", 50);
        PlanOptions o = options();
        o.breakMinutes = 10;
        QCOMPARE(layout(planDay(Queries(m_db->handle()), o, kZone)), QStringList({"09:00-09:50 a", "10:00-10:50 b"}));
    }

    void energyAndPeakHours()
    {
        add("easy", 30, Priority::High, Energy::Low);
        add("hard", 60, Priority::Medium, Energy::Peak);
        PlanOptions o = options();
        o.peak = TimeWindow{QTime(9, 0), QTime(10, 0)};
        const DayPlan p = planDay(Queries(m_db->handle()), o, kZone);
        // The easy task ranks higher but stays out of the best hour.
        QCOMPARE(layout(p), QStringList({"09:00-10:00 hard", "10:00-10:30 easy"}));
        QVERIFY(p.blocks.first().reason.contains("best hours"));

        o.energyLevel = 3; // running low: peak-energy work waits
        const DayPlan tired = planDay(Queries(m_db->handle()), o, kZone);
        QCOMPARE(layout(tired), QStringList({"10:00-10:30 easy"}));
        QCOMPARE(unplaced(tired), QStringList({"hard"}));
        QVERIFY(tired.unplaced.first().reason.contains("better day"));
    }

    void prerequisitesPlannedFirstInTheSamePlan()
    {
        add("deploy", 30, Priority::Urgent);
        add("review", 30, Priority::Low);
        add("blocked", 30, Priority::Urgent);
        add("outside", 30, Priority::Low, Energy::Medium, "home");
        Queries q(m_db->handle());
        run(ops::addDependency(q, "deploy", "review", 0));
        // Its prerequisite lives in Home hours (evening): it can't come first.
        run(ops::addDependency(q, "blocked", "outside", 0));
        const DayPlan p = planDay(q, options(), kZone);
        QCOMPARE(layout(p), QStringList({"09:00-09:30 review", "09:30-10:00 deploy", "18:00-18:30 outside"}));
        QVERIFY(p.blocks[1].reason.contains("After “review”"));
        // "blocked" waits on "outside" (18:00) but its own hours end at noon.
        QCOMPARE(unplaced(p), QStringList({"blocked"}));
    }

    void existingBlocksStayAndCountAsBusy()
    {
        add("fixed", 60);
        add("new", 60, Priority::Urgent);
        run(ops::scheduleTask("s", "fixed", at(9), at(10), 0));
        const DayPlan p = planDay(Queries(m_db->handle()), options(), kZone);
        // "fixed" already has a block so isn't planned again.
        QCOMPARE(layout(p), QStringList({"10:00-11:00 new"}));
    }

    void eventsBlockUnlessAllowed()
    {
        add("call", 30, Priority::High);
        add("deep", 30);
        run(ops::updateTask(Queries(m_db->handle()), "call", {{"tags", QStringList{"phone"}}}, 0));
        QSqlQuery(m_db->handle()).exec(QStringLiteral(
            "INSERT INTO events (id, title, is_all_day, start_at, end_at, blocking_mode, allowed_tags, created_at, updated_at) "
            "VALUES ('e', 'Commute', 0, %1, %2, 'tags_only', '[\"phone\"]', 0, 0)").arg(at(9)).arg(at(10)));
        QCOMPARE(layout(planDay(Queries(m_db->handle()), options(), kZone)), QStringList({"09:00-09:30 call", "10:00-10:30 deep"}));
    }

    void notBeforeNow()
    {
        add("a", 30);
        QCOMPARE(layout(planDay(Queries(m_db->handle()), options(11), kZone)), QStringList({"11:00-11:30 a"}));
    }

    void guidanceOrdersAndDefers()
    {
        add("a", 30, Priority::Urgent);
        add("b", 30, Priority::Low);
        add("c", 30);
        PlanOptions o = options();
        PlanGuidance g;
        g.order = {"b", "a"};
        g.defer = {{"c", "Not today: waiting for Sam's reply"}};
        g.notes = {{"b", "Quick win to get going"}};
        g.explanation = "Easy start.";
        o.guidance = g;
        const DayPlan p = planDay(Queries(m_db->handle()), o, kZone);
        QCOMPARE(layout(p), QStringList({"09:00-09:30 b", "09:30-10:00 a"}));
        QCOMPARE(p.blocks.first().reason, QStringLiteral("Quick win to get going"));
        QCOMPARE(unplaced(p), QStringList({"c"}));
        QCOMPARE(p.explanation, QStringLiteral("Easy start."));
    }

    void stepsInsteadOfParentsAndNoRoutines()
    {
        add("parent", 60, Priority::Urgent);
        add("step", 30);
        run(ops::updateTask(Queries(m_db->handle()), "step", {{"parent_task_id", "parent"}}, 0));
        Task routine;
        routine.id = "routine";
        routine.title = "routine";
        routine.categoryId = "work";
        RecurrencePattern r;
        r.startDate = kDay;
        routine.recurrence = r;
        run(ops::createTask(routine, 0));
        QCOMPARE(layout(planDay(Queries(m_db->handle()), options(), kZone)), QStringList({"09:00-09:30 step"}));
    }

    void paceStretchesBlocks()
    {
        // Five finished half-hour work tasks that each took 45 minutes.
        for (int i = 0; i < 5; ++i) {
            const QString id = QStringLiteral("done%1").arg(i);
            add(id, 30);
            QSqlQuery q(m_db->handle());
            QVERIFY(q.exec(QStringLiteral("UPDATE tasks SET status = 'completed' WHERE id = '%1'").arg(id)));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO work_logs (id, task_id, started_at, ended_at, duration_minutes, created_at) "
                                          "VALUES ('w%1', '%1', %2, %3, 45, 0)").arg(id).arg(at(7)).arg(at(7) + 45 * 60000)));
            QVERIFY(q.exec(QStringLiteral("INSERT INTO task_history (id, task_id, action, at) VALUES ('h%1', '%1', 'completed', %2)")
                               .arg(id).arg(at(8))));
        }
        add("a", 30);
        add("b", 30);
        const Queries q(m_db->handle());
        PlanOptions o = options();
        o.pace = stats::learnPace(q, at(0), at(9));
        const DayPlan p = planDay(q, o, kZone);
        QCOMPARE(layout(p), QStringList({"09:00-09:45 a", "09:45-10:30 b"}));
        QCOMPARE(p.blocks.first().estimateMinutes, 30);

        // Without it, the estimates as they are.
        QCOMPARE(layout(planDay(q, options(), kZone)), QStringList({"09:00-09:30 a", "09:30-10:00 b"}));
    }

    void maxBlocks()
    {
        for (int i = 0; i < 5; ++i)
            add(QStringLiteral("t%1").arg(i), 15);
        PlanOptions o = options();
        o.maxBlocks = 3;
        const DayPlan p = planDay(Queries(m_db->handle()), o, kZone);
        QCOMPARE(p.blocks.size(), 3);
        QCOMPARE(p.unplaced.size(), 2);
        QVERIFY(p.unplaced.first().reason.contains("full enough"));
    }
};

QTEST_GUILESS_MAIN(TestDayPlanner)
#include "tst_dayplanner.moc"
