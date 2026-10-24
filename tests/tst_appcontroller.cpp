#include "appcontroller.h"
#include "tasklistmodel.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTest>
#include <QUrl>

using namespace tumbler;

namespace {
QVariant scalar(const Database &db, const QString &sql)
{
    QSqlQuery q(db.handle());
    if (!q.exec(sql) || !q.next())
        return {};
    return q.value(0);
}
} // namespace

// Drives AppController the way the QML does, against an in-memory database.
class TestAppController : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<AppController> m_app;

    int count(const char *table) { return scalar(*m_db, QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(table))).toInt(); }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(QStringLiteral(":memory:")) && m_db->migrate());
        m_app = std::make_unique<AppController>(*m_db);
        QVERIFY(m_app->needsOnboarding());
        // A category open all day so free slots always exist whatever the clock says.
        QVariantList days;
        for (int d = 0; d < 7; ++d)
            days.append(QVariantMap{{"day", d}, {"enabled", true}, {"start", "00:00"}, {"end", "23:55"}});
        QVERIFY(!m_app->saveCategory({{"name", "Anytime"}, {"isDefault", true}, {"days", days}}).isEmpty());
        m_app->startFresh();
        QVERIFY(!m_app->needsOnboarding());
        QVERIFY(!m_app->canUndo()); // setup isn't undoable history
    }

    void cleanup()
    {
        m_app.reset();
        m_db.reset();
    }

    void captureWithTimeCreatesBlock()
    {
        const QString id = m_app->capture("call dentist tomorrow at 3pm ~15m #phone !high");
        QVERIFY(!id.isEmpty());
        const auto t = m_app->queries().task(id);
        QCOMPARE(t->title, QStringLiteral("call dentist"));
        QCOMPARE(t->priority, Priority::High);
        QCOMPARE(t->tags, QStringList({"phone"}));
        QVERIFY(!t->categoryId.isEmpty());

        const auto blocks = m_app->queries().schedulesForTask(id);
        QCOMPARE(blocks.size(), 1);
        const QDateTime start = QDateTime::fromMSecsSinceEpoch(blocks.first().start);
        QCOMPARE(start.date(), QDate::currentDate().addDays(1));
        QCOMPARE(start.time(), QTime(15, 0));
        QCOMPARE(blocks.first().end - blocks.first().start, 15 * 60000);

        // One undo removes the task and its block together.
        m_app->undo();
        QCOMPARE(count("tasks"), 0);
        QCOMPARE(count("schedules"), 0);
        m_app->redo();
        QCOMPARE(count("tasks"), 1);
        QCOMPARE(count("schedules"), 1);
    }

    void captureRecurring()
    {
        const QString id = m_app->capture("take meds every day at 8:30pm");
        const auto t = m_app->queries().task(id);
        QVERIFY(t->recurrence);
        QCOMPARE(t->scheduledTime, QTime(20, 30));
        QVERIFY(t->fixedTime);
        QVERIFY(!t->dueDate.isValid());
        // Occurrences are generated straight away: today plus two weeks.
        QVERIFY(count("task_occurrences") >= 14);
        QCOMPARE(count("schedules"), 0);
    }

    void emptyCaptureIsRefused()
    {
        QSignalSpy messages(m_app.get(), &AppController::message);
        QVERIFY(m_app->capture("  #tag  ").isEmpty());
        QCOMPARE(messages.count(), 1);
        QCOMPARE(count("tasks"), 0);
    }

    void createWithFieldsIsOneStep()
    {
        const QString id = m_app->createTask({{"title", "Report"}, {"priority", "urgent"}, {"time_estimate_minutes", 90}});
        QCOMPARE(m_app->queries().task(id)->priority, Priority::Urgent);
        QCOMPARE(m_app->queries().task(id)->estimateMinutes, 90);
        m_app->undo();
        QCOMPARE(count("tasks"), 0);
        QVERIFY(!m_app->canUndo());
    }

    void subtasksInheritCategoryAndCompleteParent()
    {
        const QString parent = m_app->createTask({{"title", "Trip"}});
        const QString a = m_app->addSubtask(parent, "Book ferry");
        const QString b = m_app->addSubtask(parent, "Pack");
        QCOMPARE(m_app->queries().task(a)->categoryId, m_app->queries().task(parent)->categoryId);

        m_app->setTaskStatus(a, "completed");
        m_app->setTaskStatus(b, "completed");
        QCOMPARE(m_app->queries().task(parent)->status, TaskStatus::Completed);

        const QVariantMap detail = m_app->taskDetail(parent);
        QCOMPARE(detail.value("subtasks").toList().size(), 2);
        QCOMPARE(detail.value("subtasks").toList().first().toMap().value("title").toString(), QStringLiteral("Book ferry"));
    }

    void schedulingAndBumping()
    {
        const QString id = m_app->createTask({{"title", "Focus"}, {"time_estimate_minutes", 30}});
        QVERIFY(m_app->scheduleNextSlot(id));
        auto block = m_app->queries().schedulesForTask(id).first();
        QVERIFY(block.start >= QDateTime::currentMSecsSinceEpoch() - 60000);
        QCOMPARE(block.end - block.start, 30 * 60000);

        QVERIFY(m_app->bump(block.id, 15));
        auto moved = *m_app->queries().schedule(block.id);
        QCOMPARE(moved.start, block.start + 15 * 60000);
        QCOMPARE(moved.bumpedCount, 1);

        QVERIFY(m_app->bumpToTomorrow(block.id));
        moved = *m_app->queries().schedule(block.id);
        QCOMPARE(QDateTime::fromMSecsSinceEpoch(moved.start).date(), QDate::currentDate().addDays(1));
        QCOMPARE(moved.bumpedCount, 2);

        // Ready to Work excludes it while it has an open block.
        for (const QVariant &v : m_app->today().value("ready").toList())
            QVERIFY(v.toMap().value("id").toString() != id);

        QVERIFY(m_app->completeSchedule(block.id));
        QCOMPARE(m_app->queries().task(id)->status, TaskStatus::Completed);
        m_app->undo();
        QCOMPARE(m_app->queries().task(id)->status, TaskStatus::Pending);
        QVERIFY(!m_app->queries().schedule(block.id)->completed);
    }

    void movingPastDueDateIsAllowedWithANote()
    {
        const QString id = m_app->createTask({{"title", "Invoice"}, {"due_date", QDate::currentDate().toString(Qt::ISODate)}});
        QVERIFY(m_app->scheduleAt(id, QDateTime(QDate::currentDate(), QTime(23, 0)).toMSecsSinceEpoch(), 30));
        const QString block = m_app->queries().schedulesForTask(id).first().id;
        QSignalSpy messages(m_app.get(), &AppController::message);
        QVERIFY(m_app->bumpToTomorrow(block));
        QCOMPARE(QDateTime::fromMSecsSinceEpoch(m_app->queries().schedule(block)->start).date(), QDate::currentDate().addDays(1));
        QVERIFY(messages.last().first().toString().contains("after its due date"));
    }

    void workTimer()
    {
        const QString id = m_app->createTask({{"title", "Write"}});
        QSignalSpy working(m_app.get(), &AppController::workingChanged);
        QVERIFY(m_app->startWork(id));
        QCOMPARE(m_app->working().value("taskId").toString(), id);
        QCOMPARE(m_app->working().value("title").toString(), QStringLiteral("Write"));
        QVERIFY(working.count() >= 1);
        QVERIFY(m_app->stopWork());
        QVERIFY(m_app->working().isEmpty());
        QVERIFY(!m_app->stopWork()); // nothing running now
    }

    void dependenciesBlockReady()
    {
        const QString review = m_app->createTask({{"title", "Review"}});
        const QString deploy = m_app->createTask({{"title", "Deploy"}});
        QVERIFY(m_app->addDependency(deploy, review));
        QVERIFY(!m_app->addDependency(review, deploy)); // loop refused

        QStringList ready;
        for (const QVariant &v : m_app->today().value("ready").toList())
            ready.append(v.toMap().value("title").toString());
        QCOMPARE(ready, QStringList({"Review"}));

        bool candidate = false;
        for (const QVariant &v : m_app->prerequisiteCandidates(review))
            candidate |= v.toMap().value("id").toString() == deploy;
        QVERIFY(!candidate);
    }

    void taskListFilters()
    {
        TaskListModel *list = m_app->tasks();
        m_app->capture("buy oat milk #errands");
        const QString done = m_app->capture("file taxes");
        m_app->setTaskStatus(done, "completed");
        const QString routine = m_app->capture("stretch daily");
        Q_UNUSED(routine);

        QCOMPARE(list->rowCount(), 2); // active: oat milk, stretch
        QCOMPARE(list->data(list->index(0), TaskListModel::TitleRole).toString(), QStringLiteral("buy oat milk")); // one-offs first
        list->setFilter("completed");
        QCOMPARE(list->rowCount(), 1);
        list->setFilter("all");
        list->setSearch("#errands");
        QCOMPARE(list->rowCount(), 1);
        list->setSearch(QString());
        QCOMPARE(list->rowCount(), 3);
        QCOMPARE(list->totalActive(), 2);
    }

    void categoriesAndSettings()
    {
        const QString work = m_app->saveCategory({{"name", "Work"}, {"color", "#0ea5e9"}});
        QCOMPARE(m_app->categories().size(), 2);
        const QString id = m_app->capture("standup notes /work");
        QCOMPARE(m_app->queries().task(id)->categoryId, work);
        QVERIFY(m_app->deleteCategory(work));
        QVERIFY(m_app->queries().task(id)->categoryId != work);

        QVERIFY(m_app->setSetting("reminder_minutes", 10));
        QCOMPARE(m_app->settings().value("reminderMinutes").toInt(), 10);
        m_app->undo();
        QCOMPARE(m_app->settings().value("reminderMinutes").toInt(), 5);
    }

    void dayAgendaShape()
    {
        const QString id = m_app->capture("deep work today at 10am ~2h");
        Q_UNUSED(id);
        const QVariantMap day = m_app->dayAgenda(QDate::currentDate().toString(Qt::ISODate));
        QVERIFY(day.value("isToday").toBool());
        QCOMPARE(day.value("items").toList().size(), 1);
        QCOMPARE(day.value("windows").toList().size(), 1); // the default category
        QVERIFY(m_app->dayAgenda("not a date").isEmpty());
    }

    void newDatabaseGetsAnAnytimeDefault()
    {
        Database fresh(QStringLiteral("anytime"));
        QVERIFY(fresh.open(QStringLiteral(":memory:")) && fresh.migrate());
        AppController app(fresh);
        app.startFresh();
        const auto def = app.queries().defaultCategory();
        QVERIFY(def);
        QCOMPARE(def->name, QStringLiteral("Default"));
        QVERIFY(def->showOnSchedule);
        for (int day = 0; day < 7; ++day)
            QCOMPARE(def->schedule.windowForWeekday(day), std::optional<TimeWindow>(TimeWindow{QTime(6, 0), QTime(23, 0)}));
        QVERIFY(!app.canUndo());
        app.startFresh(); // idempotent
        QCOMPARE(app.categories().size(), 1);
    }

    void captureBeforeOnboardingCreatesDefault()
    {
        Database fresh(QStringLiteral("early"));
        QVERIFY(fresh.open(QStringLiteral(":memory:")) && fresh.migrate());
        AppController app(fresh);
        const QString id = app.capture("water the plants");
        const auto def = app.queries().defaultCategory();
        QVERIFY(def);
        QCOMPARE(app.queries().task(id)->categoryId, def->id);
        // Undoing the capture removes the task, not the category.
        app.undo();
        QCOMPARE(app.queries().tasks().size(), 0);
        QVERIFY(app.queries().defaultCategory());
    }

    void importFromOnboarding()
    {
        Database fresh(QStringLiteral("fresh"));
        QVERIFY(fresh.open(QStringLiteral(":memory:")) && fresh.migrate());
        AppController app(fresh);
        QVERIFY(app.needsOnboarding());
        const QString report = app.importExport(QUrl::fromLocalFile(QStringLiteral(TUMBLER_FIXTURES "/export_quirks.json")));
        QVERIFY2(report.startsWith("Imported 6 tasks"), qPrintable(report));
        QVERIFY(!app.needsOnboarding());
        QVERIFY(!app.canUndo());
        QCOMPARE(app.tasks()->totalActive(), 5); // all but the completed subtask

        // A second import is refused and changes nothing.
        QVERIFY(app.importExport(QUrl::fromLocalFile(QStringLiteral(TUMBLER_FIXTURES "/export_quirks.json"))).startsWith("Import failed"));
        QVERIFY(app.importExport(QUrl::fromLocalFile("/nonexistent.json")).startsWith("Couldn't open"));
    }
};

QTEST_GUILESS_MAIN(TestAppController)
#include "tst_appcontroller.moc"
