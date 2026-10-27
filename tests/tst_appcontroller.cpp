#include "appcontroller.h"
#include "desktop/devicecalendars.h"
#include "desktop/externalapi.h"
#include "ipc.h"
#include "keystore.h"
#include "remotehub.h"
#include "synchub.h"
#include "fakeserver.h"
#include "tasklistmodel.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTest>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>

using namespace tumbler;

namespace {
// Records keyring access instead of opening KWallet.
class FakeKeyStore : public KeyStore
{
public:
    void read(const QString &key, std::function<void(const QString &, const QString &)> done) override
    {
        reads.append(key);
        done(QStringLiteral("sk-from-keyring"), {});
    }
    void write(const QString &key, const QString &, std::function<void(const QString &)> done) override
    {
        writes.append(key);
        done({});
    }
    QStringList reads, writes;
};

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

private:
    // ---- Phase 2 helpers ---------------------------------------------------

    QVariantMap waitForPlan(AppController &app, const std::function<void()> &trigger)
    {
        QSignalSpy spy(&app, &AppController::planReady);
        trigger();
        if (spy.isEmpty())
            spy.wait(5000);
        return spy.isEmpty() ? QVariantMap() : spy.first().first().toMap();
    }

    void useOllama(FakeServer &server)
    {
        m_app->setSetting("ai_provider", "ollama");
        m_app->setSetting("ollama_url", server.url().toString());
        m_app->setSetting("ollama_model", "test-model");
        QVERIFY(m_app->ai().value("ready").toBool());
    }

    static QJsonObject ollamaAnswer(const QJsonObject &content)
    {
        return {{"message", QJsonObject{{"role", "assistant"}, {"content", QString::fromUtf8(QJsonDocument(content).toJson())}}}};
    }

    QVariantList titlesOf(const QVariantList &items)
    {
        QVariantList out;
        for (const QVariant &v : items)
            out.append(v.toMap().value("title"));
        return out;
    }

private slots:
    void offlinePlanAcceptedAsOneStep()
    {
        m_app->createTask({{"title", "Report"}, {"priority", "urgent"}, {"time_estimate_minutes", 30}});
        m_app->createTask({{"title", "Email"}, {"time_estimate_minutes", 15}});
        const QString tomorrow = QDate::currentDate().addDays(1).toString(Qt::ISODate);
        const QVariantMap plan = waitForPlan(*m_app, [&] { m_app->planDay(tomorrow, false); });
        QCOMPARE(titlesOf(plan.value("blocks").toList()), QVariantList({"Report", "Email"}));
        QVERIFY(!plan.value("usedAi").toBool());
        // Tomorrow, in the Anytime category's hours, starting at midnight.
        QCOMPARE(QDateTime::fromMSecsSinceEpoch(plan.value("blocks").toList().first().toMap().value("start").toLongLong()).date(),
                 QDate::currentDate().addDays(1));

        QVERIFY(m_app->acceptPlan(plan.value("blocks").toList(), plan));
        QCOMPARE(count("schedules"), 2);
        m_app->undo();
        QCOMPARE(count("schedules"), 0);
        QVERIFY(!m_app->acceptPlan({}, plan)); // nothing chosen
    }

    void aiPlanRanksAndDefers()
    {
        FakeServer server;
        useOllama(server);
        const QString a = m_app->createTask({{"title", "Big report"}, {"priority", "urgent"}});
        const QString b = m_app->createTask({{"title", "Water plants"}, {"priority", "low"}, {"time_estimate_minutes", 10}});
        server.reply(200, ollamaAnswer({{"order", QJsonArray{b}},
                                        {"defer", QJsonArray{QJsonObject{{"task_id", a}, {"reason", "Needs more energy than today has"}}}},
                                        {"notes", QJsonArray{QJsonObject{{"task_id", b}, {"note", "Easy start"}}}},
                                        {"explanation", "A gentle day."}}));
        const QString tomorrow = QDate::currentDate().addDays(1).toString(Qt::ISODate);
        const QVariantMap plan = waitForPlan(*m_app, [&] { m_app->planDay(tomorrow, true); });
        QVERIFY(plan.value("usedAi").toBool());
        QCOMPARE(titlesOf(plan.value("blocks").toList()), QVariantList({"Water plants"}));
        QCOMPARE(plan.value("blocks").toList().first().toMap().value("reason").toString(), QStringLiteral("Easy start"));
        QCOMPARE(plan.value("unplaced").toList().first().toMap().value("reason").toString(), QStringLiteral("Needs more energy than today has"));
        QCOMPARE(plan.value("explanation").toString(), QStringLiteral("A gentle day."));
        QVERIFY(!m_app->ai().value("busy").toBool());

        // The prompt carried both tasks; the call was logged and the outcome recorded.
        const QString prompt = server.requests.first().body.value("messages").toArray().last().toObject().value("content").toString();
        QVERIFY(prompt.contains("Big report") && prompt.contains("Water plants"));
        QCOMPARE(count("ai_logs"), 1);
        m_app->acceptPlan(plan.value("blocks").toList(), plan);
        QCOMPARE(scalar(*m_db, "SELECT user_feedback FROM ai_logs").toString(), QStringLiteral("accepted"));
    }

    void aiFailureFallsBackToOfflinePlan()
    {
        FakeServer server;
        useOllama(server);
        m_app->createTask({{"title", "Report"}});
        server.reply(500, QJsonObject{{"error", "out of memory"}});
        const QVariantMap plan = waitForPlan(*m_app, [&] { m_app->planDay(QDate::currentDate().addDays(1).toString(Qt::ISODate), true); });
        QVERIFY(!plan.value("usedAi").toBool());
        QVERIFY(!plan.value("aiError").toString().isEmpty());
        QCOMPARE(titlesOf(plan.value("blocks").toList()), QVariantList({"Report"}));
    }

    void breakDownIntoSteps()
    {
        FakeServer server;
        useOllama(server);
        const QString id = m_app->createTask({{"title", "Do taxes"}});
        server.reply(200, ollamaAnswer({{"steps", QJsonArray{QJsonObject{{"title", "Find last year's return"}, {"minutes", 10}, {"energy", "low"}},
                                                             QJsonObject{{"title", "Fill in income"}, {"minutes", 45}, {"energy", "high"}}}},
                                        {"explanation", "Start by finding the paperwork."}}));
        QSignalSpy spy(m_app.get(), &AppController::stepsReady);
        m_app->breakDown(id);
        QVERIFY(spy.wait(5000));
        const QVariantList steps = spy.first().at(1).toList();
        QCOMPARE(titlesOf(steps), QVariantList({"Find last year's return", "Fill in income"}));

        QVERIFY(m_app->addSteps(id, steps));
        const QVariantList sub = m_app->taskDetail(id).value("subtasks").toList();
        QCOMPARE(titlesOf(sub), QVariantList({"Find last year's return", "Fill in income"})); // order kept
        QCOMPARE(sub.last().toMap().value("energy").toString(), QStringLiteral("high"));
        m_app->undo();
        QVERIFY(m_app->taskDetail(id).value("subtasks").toList().isEmpty());
    }

    void aiCaptureFeedsCapture()
    {
        FakeServer server;
        useOllama(server);
        const QString text = "need to sort out the car thing before my trip next tuesday";
        const QDate tuesday = QDate::currentDate().addDays((2 - QDate::currentDate().dayOfWeek() % 7 + 7) % 7 + 7);
        server.reply(200, ollamaAnswer({{"title", "Get the car serviced"},
                                        {"priority", "high"},
                                        {"energy", "low"},
                                        {"estimate_minutes", 60},
                                        {"date", tuesday.toString(Qt::ISODate)},
                                        {"time", ""},
                                        {"tags", QJsonArray{"car"}},
                                        {"category", ""},
                                        {"recurrence", QJsonObject{{"type", "none"}, {"interval", 1}, {"days", QJsonArray{}}}}}));
        QSignalSpy spy(m_app.get(), &AppController::aiCaptureReady);
        m_app->aiCapture(text);
        QVERIFY(spy.wait(5000));
        QVERIFY(spy.first().at(1).toMap().value("fromAi").toBool());
        QCOMPARE(m_app->previewCapture(text).value("title").toString(), QStringLiteral("Get the car serviced"));

        const QString id = m_app->capture(text);
        const auto t = m_app->queries().task(id);
        QCOMPARE(t->title, QStringLiteral("Get the car serviced"));
        QCOMPARE(t->dueDate, tuesday);
        QCOMPARE(t->tags, QStringList({"car"}));
        // Used once: the same text again goes back to offline parsing.
        QVERIFY(!m_app->previewCapture(text).value("fromAi").toBool());
    }

    void energyAndHealth()
    {
        QVERIFY(!m_app->energy().contains("level"));
        QVERIFY(m_app->logEnergy(3));
        QCOMPARE(m_app->energy().value("level").toInt(), 3);
        QCOMPARE(m_app->energy().value("label").toString(), QStringLiteral("Low"));
        QVERIFY(!m_app->energy().value("due").toBool());

        QVERIFY(m_app->saveHealth({{"sleep", "64"}, {"readiness", ""}, {"hours", "6.5"}}));
        QCOMPARE(m_app->healthToday().value("sleep").toInt(), 64);
        QVERIFY(m_app->healthToday().value("readiness").isNull());
        QCOMPARE(m_app->healthToday().value("hours").toDouble(), 6.5);

        const QVariantMap history = m_app->energyHistory(7);
        QCOMPARE(history.value("days").toList().size(), 7);
        QCOMPARE(history.value("days").toList().last().toMap().value("energy").toDouble(), 3.0);
        QCOMPARE(history.value("days").toList().last().toMap().value("sleep").toInt(), 64);
        QCOMPARE(history.value("energy").toList().size(), 1);

        // A low check-in today keeps peak-energy work out of today's plan.
        m_app->createTask({{"title", "Deep design work"}, {"energy_required", "peak"}});
        const QVariantMap plan = waitForPlan(*m_app, [&] { m_app->planDay(QDate::currentDate().toString(Qt::ISODate), false); });
        QVERIFY(plan.value("blocks").toList().isEmpty());
        QVERIFY(plan.value("unplaced").toList().first().toMap().value("reason").toString().contains("better day"));
    }

    void eventsFromTheApp()
    {
        const QString date = QDate::currentDate().toString(Qt::ISODate);
        const QString id = m_app->saveEvent({{"title", "Night shift"}, {"date", date}, {"startTime", "22:00"}, {"endTime", "02:00"},
                                             {"blockingMode", "block_all"}, {"color", "#ec4899"}});
        QVERIFY(!id.isEmpty());
        const QVariantMap e = m_app->eventDetail(id);
        QCOMPARE(e.value("startTime").toString(), QStringLiteral("22:00"));
        QCOMPARE(e.value("endDate").toString(), QDate::currentDate().addDays(1).toString(Qt::ISODate)); // ran past midnight
        QCOMPARE(e.value("blockingMode").toString(), QStringLiteral("block_all"));

        QVERIFY(!m_app->saveEvent({{"id", id}, {"title", "Night shift"}, {"allDay", false}, {"date", date}}).size()); // no times
        QVERIFY(!m_app->saveEvent({{"id", id}, {"title", "Holiday"}, {"allDay", true}, {"date", date}}).isEmpty());
        QVERIFY(m_app->eventDetail(id).value("allDay").toBool());

        QVERIFY(m_app->deleteEvent(id));
        QVERIFY(m_app->eventDetail(id).isEmpty());
        m_app->undo();
        QCOMPARE(m_app->eventDetail(id).value("title").toString(), QStringLiteral("Holiday"));
    }

    void aiOffAndSettings()
    {
        QVERIFY(!m_app->ai().value("ready").toBool());
        QSignalSpy messages(m_app.get(), &AppController::message);
        m_app->breakDown(m_app->createTask({{"title", "x"}}));
        QVERIFY(messages.last().first().toString().contains("Settings"));
        m_app->setSetting("ai_provider", "anthropic");
        QVERIFY(!m_app->ai().value("ready").toBool()); // no key yet
        m_app->setApiKey("sk-test");                  // no keyring in tests: kept in memory
        QVERIFY(m_app->ai().value("ready").toBool());
        QCOMPARE(m_app->ai().value("label").toString(), QStringLiteral("Claude (claude-opus-5-5)"));
    }

    void launchersAndApproval()
    {
        const QString id = m_app->createTask({{"title", "Write report"}});
        QVERIFY(!m_app->addAction(id, "url", "docs.example.com/report", true).isEmpty());
        const QString cmd = m_app->addAction(id, "command", "kate \"/home/me/My Report.md\"", true);
        QVERIFY(!cmd.isEmpty());
        QVERIFY(m_app->addAction(id, "command", "  ", true).isEmpty()); // nothing to run

        const QVariantList actions = m_app->taskDetail(id).value("actions").toList();
        QCOMPARE(actions.size(), 2);
        QCOMPARE(actions[0].toMap().value("target").toString(), QStringLiteral("https://docs.example.com/report"));
        QCOMPARE(actions[1].toMap().value("target").toString(), QStringLiteral("[\"kate\",\"/home/me/My Report.md\"]"));
        QVERIFY(actions[1].toMap().value("needsApproval").toBool());

        // Starting: the URL opens; the unapproved command asks instead of running.
        QSignalSpy launches(m_app.get(), &AppController::launchRequested);
        QSignalSpy asks(m_app.get(), &AppController::commandNeedsApproval);
        QVERIFY(m_app->startWork(id));
        QCOMPARE(launches.count(), 1);
        QCOMPARE(launches.first().first().toMap().value("kind").toString(), QStringLiteral("url"));
        QCOMPARE(asks.count(), 1);
        QCOMPARE(asks.first().at(2).toString(), QStringLiteral("kate \"/home/me/My Report.md\""));

        // Approving runs it with the exact argv, and it no longer asks.
        m_app->approveCommand(cmd);
        QCOMPARE(launches.count(), 2);
        QCOMPARE(launches.last().first().toMap().value("argv").toStringList(), QStringList({"kate", "/home/me/My Report.md"}));
        m_app->runAction(cmd);
        QCOMPARE(launches.count(), 3);
        QCOMPARE(asks.count(), 1);

        // A different command, even slightly, asks again.
        const QString other = m_app->addAction(id, "command", "kate /home/me/other.md", false);
        m_app->runAction(other);
        QCOMPARE(asks.count(), 2);
        QVERIFY(m_app->removeAction(other));
    }

    void splitsCommandsWithoutAShell()
    {
        QCOMPARE(AppController::splitCommand("code  ~/proj   --new-window"), QStringList({"code", "~/proj", "--new-window"}));
        QCOMPARE(AppController::splitCommand("mpv 'a b.mp3' \"c \\\"d\\\".mp3\" e\\ f"), QStringList({"mpv", "a b.mp3", "c \"d\".mp3", "e f"}));
        QCOMPARE(AppController::splitCommand("echo $HOME | rm -rf /"), QStringList({"echo", "$HOME", "|", "rm", "-rf", "/"})); // literal words, no shell
        QVERIFY(AppController::splitCommand("   ").isEmpty());
    }

    void timeAway()
    {
        const QString id = m_app->createTask({{"title", "Deep work"}});
        // Not working: nothing to ask about.
        m_app->reportAway(m_app->now() - 30 * 60000, m_app->now());
        QVERIFY(m_app->away().isEmpty());

        QVERIFY(m_app->startWork(id));
        QSqlQuery(m_db->handle()).exec(QStringLiteral("UPDATE work_logs SET started_at = %1").arg(m_app->now() - 60 * 60000));
        const qint64 left = m_app->now() - 30 * 60000;
        m_app->reportAway(left, m_app->now());
        QCOMPARE(m_app->away().value("minutes").toInt(), 30);
        QCOMPARE(m_app->away().value("title").toString(), QStringLiteral("Deep work"));

        m_app->resolveAway("trimRestart");
        QVERIFY(m_app->away().isEmpty());
        // The first session ended when they left (about 30 min) and a new one runs.
        QCOMPARE(scalar(*m_db, "SELECT duration_minutes FROM work_logs WHERE ended_at IS NOT NULL").toInt(), 30);
        QCOMPARE(m_app->working().value("taskId").toString(), id);

        m_app->reportAway(m_app->now() - 15 * 60000, m_app->now()); // session started after they left: ignored
        QVERIFY(m_app->away().isEmpty());
    }

    void krunnerMatches()
    {
        const QString id = m_app->createTask({{"title", "Renew passport"}});
        QVariantList m = m_app->runnerMatches("tt call mum tomorrow at 6pm #family");
        QCOMPARE(m.size(), 1);
        QCOMPARE(m.first().toMap().value("text").toString(), QStringLiteral("Add task: call mum"));
        QVERIFY(m.first().toMap().value("subtext").toString().contains("#family"));
        QVERIFY(m_app->runnerMatches("tt").isEmpty());
        QVERIFY(m_app->runnerMatches("re").isEmpty()); // too short to search

        m = m_app->runnerMatches("passport");
        QCOMPARE(m.size(), 1);
        QCOMPARE(m.first().toMap().value("id").toString(), QStringLiteral("task:") + id);

        QVERIFY(m_app->runRunnerMatch("add:call mum tomorrow", {}));
        QCOMPARE(m_app->externalTasks("open", "mum").size(), 1);
        QVERIFY(m_app->runRunnerMatch("task:" + id, "start"));
        QCOMPARE(m_app->working().value("taskId").toString(), id);
        QSignalSpy open(m_app.get(), &AppController::openTaskRequested);
        QVERIFY(m_app->runRunnerMatch("task:" + id, {}));
        QCOMPARE(open.first().first().toString(), id);
        QVERIFY(m_app->runRunnerMatch("task:" + id, "done"));
        QCOMPARE(m_app->queries().task(id)->status, TaskStatus::Completed);
    }

    void dbusNotes()
    {
        ExternalApi api(m_app.get());
        const QString task = m_app->createTask({{"title", "Release"}});
        const QString id = api.AddNote("Release <checklist>", "tag & push", task, "work, release");
        QVERIFY(!id.isEmpty());
        QCOMPARE(m_app->undoText(), QStringLiteral("Add note “Release <checklist>”"));
        QVERIFY(api.AddNote("x", "y", "no-such-task", "").isEmpty());

        QVERIFY(api.AppendToNote(id, "- write changelog"));
        QCOMPARE(m_app->undoText(), QStringLiteral("Add to note “Release <checklist>”"));
        QVERIFY(!api.AppendToNote(id, "   "));
        const QJsonObject note = QJsonDocument::fromJson(api.Note(id).toUtf8()).object();
        QCOMPARE(note.value("content").toString(), QStringLiteral("tag & push\n\n- write changelog"));
        QCOMPARE(note.value("taskTitle").toString(), QStringLiteral("Release"));
        QCOMPARE(note.value("tags").toArray().size(), 2);

        // Snippets come back as plain text, not the UI's markup.
        const QJsonArray found = QJsonDocument::fromJson(api.Notes("push", "").toUtf8()).array();
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().toObject().value("snippet").toString(), QStringLiteral("tag & push - write changelog"));
        QCOMPARE(QJsonDocument::fromJson(api.Notes("", "release").toUtf8()).array().size(), 1);

        m_app->undo();
        QCOMPARE(QJsonDocument::fromJson(api.Note(id).toUtf8()).object().value("content").toString(), QStringLiteral("tag & push"));

        m_app->setSetting("external_writes", false);
        QVERIFY(api.AddNote("blocked", "", "", "").isEmpty());
        QVERIFY(!api.AppendToNote(id, "blocked"));
        m_app->setSetting("external_writes", true);
    }

    void schedulingAllowsForPace()
    {
        // Five finished half-hour tasks that each took an hour.
        for (int i = 0; i < 5; ++i) {
            const QString id = m_app->createTask({{"title", QStringLiteral("Done %1").arg(i)}, {"time_estimate_minutes", 30}});
            const qint64 start = m_app->now() - 24 * 3600000LL;
            QVERIFY(QSqlQuery(m_db->handle()).exec(QStringLiteral(
                "INSERT INTO work_logs (id, task_id, started_at, ended_at, duration_minutes, created_at) VALUES ('w%1', '%2', %3, %4, 60, %3)")
                .arg(i).arg(id).arg(start).arg(start + 60 * 60000)));
            QVERIFY(m_app->setTaskStatus(id, "completed"));
        }
        const auto length = [this](const QString &taskId) {
            const QList<ScheduleBlock> blocks = m_app->queries().schedulesForTask(taskId);
            return blocks.isEmpty() ? 0 : int((blocks.first().end - blocks.first().start) / 60000);
        };

        const QString next = m_app->createTask({{"title", "Next"}, {"time_estimate_minutes", 30}});
        QVERIFY(m_app->scheduleNextSlot(next));
        QCOMPARE(length(next), 60);
        const QString at = m_app->createTask({{"title", "At"}, {"time_estimate_minutes", 30}});
        QVERIFY(m_app->scheduleAt(at, m_app->now() + 3 * 3600000LL, 0));
        QCOMPARE(length(at), 60);
        QVERIFY(m_app->scheduleAt(m_app->createTask({{"title", "Exact"}}), m_app->now() + 5 * 3600000LL, 20)); // a chosen length stands

        // A stated estimate in capture is taken at its word; an unstated one isn't.
        QCOMPARE(length(m_app->capture("stated tomorrow at 3pm ~30m")), 30);
        QCOMPARE(length(m_app->capture("unstated tomorrow at 5pm")), 60);

        m_app->setSetting("plan_with_pace", false);
        const QString off = m_app->createTask({{"title", "Off"}, {"time_estimate_minutes", 30}});
        QVERIFY(m_app->scheduleNextSlot(off));
        QCOMPARE(length(off), 30);
        QVERIFY(!m_app->stats(7).value("paceActive").toBool());
    }

    void closeOutLeftovers()
    {
        const auto blockOf = [this](const QString &taskId) {
            const QList<ScheduleBlock> blocks = m_app->queries().schedulesForTask(taskId);
            return blocks.isEmpty() ? QString() : blocks.first().id;
        };
        QStringList ids;
        for (const char *title : {"Pile", "Drop", "Tomorrow", "Rest"}) {
            const QString id = m_app->createTask({{"title", title}, {"time_estimate_minutes", 30}});
            QVERIFY(m_app->scheduleAt(id, m_app->now() - (ids.size() + 2) * 3600000LL, 30));
            ids.append(id);
        }
        QCOMPARE(m_app->closeOut().value("leftovers").toList().size(), 4);

        QVERIFY(m_app->resolveLeftover("block", blockOf(ids[0]), "pile"));
        QVERIFY(blockOf(ids[0]).isEmpty());
        QCOMPARE(m_app->queries().task(ids[0])->status, TaskStatus::Pending);

        QVERIFY(m_app->resolveLeftover("block", blockOf(ids[1]), "drop"));
        QCOMPARE(m_app->queries().task(ids[1])->status, TaskStatus::Cancelled);
        QVERIFY(blockOf(ids[1]).isEmpty());
        m_app->undo(); // one step puts both back
        QCOMPARE(m_app->queries().task(ids[1])->status, TaskStatus::Pending);
        QVERIFY(!blockOf(ids[1]).isEmpty());
        m_app->redo();

        QVERIFY(m_app->resolveLeftover("block", blockOf(ids[2]), "tomorrow"));
        const QDate tomorrow = QDateTime::currentDateTime().date().addDays(1);
        QVERIFY(m_app->queries().schedule(blockOf(ids[2]))->start >= QDateTime(tomorrow, QTime(0, 0)).toMSecsSinceEpoch());

        const QVariantList left = m_app->closeOut().value("leftovers").toList();
        QCOMPARE(left.size(), 1);
        QCOMPARE(left[0].toMap().value("taskId").toString(), ids[3]);
        QVERIFY(m_app->clearLeftovers());
        QVERIFY(m_app->closeOut().value("leftovers").toList().isEmpty());
        QCOMPARE(m_app->undoText(), QStringLiteral("Clear the rest of the day"));
        m_app->undo();
        QCOMPARE(m_app->closeOut().value("leftovers").toList().size(), 1);
        QVERIFY(!m_app->resolveLeftover("block", blockOf(ids[3]), "nonsense"));
    }

    void closeOutNoteAndNudges()
    {
        QVERIFY(m_app->saveDayNote("   ").isEmpty()); // nothing written, nothing made
        const QString id = m_app->saveDayNote("Walk helped");
        QVERIFY(!id.isEmpty());
        QCOMPARE(m_app->saveDayNote("Walk helped a lot"), id); // the same note all day
        QCOMPARE(m_app->closeOut().value("note").toString(), QStringLiteral("Walk helped a lot"));
        QCOMPARE(m_app->notes({}, "day").size(), 1);
        QVERIFY(!m_app->canUndo()); // typing isn't undo history

        m_app->setSetting("closeout_time", "00:00");
        QVERIFY(m_app->today().value("closeOutDue").toBool());
        m_app->finishCloseOut(true);
        QVERIFY(!m_app->today().value("closeOutDue").toBool());
        QVERIFY(m_app->closeOut().value("closed").toBool());
        m_app->setSetting("closeout_last", "");
        m_app->setSetting("closeout_time", "");
        QVERIFY(!m_app->today().value("closeOutDue").toBool()); // off

        m_app->setSetting("review_weekday", QDate::currentDate().dayOfWeek() % 7);
        QVERIFY(m_app->today().value("reviewDue").toBool());
        m_app->finishReview(true);
        QVERIFY(!m_app->today().value("reviewDue").toBool());
        QVERIFY(m_app->weekReview().value("doneToday").toBool());
        m_app->setSetting("review_last", "");
        m_app->setSetting("review_weekday", (QDate::currentDate().dayOfWeek() + 1) % 7); // tomorrow: last done 6 days ago
        QVERIFY(m_app->today().value("reviewDue").toBool());
        m_app->setSetting("review_weekday", -1);
        QVERIFY(!m_app->today().value("reviewDue").toBool());
    }

    void weekReviewLists()
    {
        const QString done = m_app->createTask({{"title", "Shipped it"}});
        QVERIFY(m_app->setTaskStatus(done, "completed"));
        const QString due = m_app->createTask({{"title", "Renew passport"}, {"due_date", QDate::currentDate().addDays(2).toString(Qt::ISODate)}});
        const QVariantMap w = m_app->weekReview();
        QCOMPARE(w.value("days").toList().size(), 7);
        QCOMPARE(w.value("wins").toList().size(), 1);
        QCOMPARE(w.value("wins").toList()[0].toMap().value("taskId").toString(), done);
        QCOMPARE(w.value("dueSoon").toList().size(), 1);
        QCOMPARE(w.value("dueSoon").toList()[0].toMap().value("taskId").toString(), due);

        QVERIFY(m_app->letGo(due));
        QCOMPARE(m_app->queries().task(due)->status, TaskStatus::Cancelled);
        QVERIFY(m_app->weekReview().value("dueSoon").toList().isEmpty());

        QSignalSpy failed(m_app.get(), &AppController::aiFailed);
        m_app->summarizeWeek(); // no AI set up
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed[0][0].toString(), QStringLiteral("week"));
    }

    void statsAndDBusStats()
    {
        QVERIFY(m_app->stats(7).value("tasks").toList().isEmpty());

        const QString id = m_app->createTask({{"title", "Write report"}, {"time_estimate_minutes", 30}});
        const qint64 start = m_app->now() - 3 * 24 * 3600000LL;
        QVERIFY(QSqlQuery(m_db->handle()).exec(QStringLiteral(
            "INSERT INTO work_logs (id, task_id, started_at, ended_at, duration_minutes, created_at) VALUES ('w1', '%1', %2, %3, 45, %2)")
            .arg(id).arg(start).arg(start + 45 * 60000)));
        QVERIFY(m_app->setTaskStatus(id, "completed"));

        const QVariantMap week = m_app->stats(7);
        QCOMPARE(week.value("bars").toList().size(), 7);
        QVERIFY(!week.value("weekly").toBool());
        QCOMPARE(week.value("workedMinutes").toInt(), 45);
        QCOMPARE(week.value("finishedTasks").toInt(), 1);
        QCOMPARE(week.value("tasks").toList().first().toMap().value("title").toString(), QStringLiteral("Write report"));
        QCOMPARE(week.value("estimates").toMap().value("ratio").toDouble(), 1.5);
        QCOMPARE(week.value("hours").toList().size(), 24);

        // Everything starts at the first activity; long ranges go weekly.
        QCOMPARE(m_app->stats(0).value("from").toString(),
                 QDateTime::fromMSecsSinceEpoch(start, m_app->zone()).date().toString(Qt::ISODate));
        QVERIFY(m_app->stats(90).value("weekly").toBool());

        ExternalApi api(m_app.get());
        const QJsonObject stats = QJsonDocument::fromJson(api.Stats(7).toUtf8()).object();
        QCOMPARE(stats.value("workedMinutes").toInt(), 45);
        QVERIFY(stats.value("hours").toArray().size() <= 2); // only the hours with work in them
    }

    void dbusApiAndWriteSwitch()
    {
        ExternalApi api(m_app.get());
        const QString id = api.Capture("water plants every day");
        QVERIFY(!id.isEmpty());
        const QJsonArray tasks = QJsonDocument::fromJson(api.Tasks("open", "").toUtf8()).array();
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().toObject().value("title").toString(), QStringLiteral("water plants"));
        QVERIFY(QJsonDocument::fromJson(api.Today().toUtf8()).object().contains("upcoming"));
        QCOMPARE(QJsonDocument::fromJson(api.Task(id).toUtf8()).object().value("title").toString(), QStringLiteral("water plants"));
        QVERIFY(!QJsonDocument::fromJson(api.Task(id).toUtf8()).object().contains("actions"));

        m_app->setSetting("external_writes", false);
        QVERIFY(api.Capture("blocked").isEmpty());
        QVERIFY(!api.Complete(id));
        QVERIFY(!m_app->runRunnerMatch("add:blocked too", {}));
        QCOMPARE(QJsonDocument::fromJson(api.Tasks("all", "").toUtf8()).array().size(), 1); // reads still work
        m_app->setSetting("external_writes", true);
        QVERIFY(api.Complete(id));
        QVERIFY(QJsonDocument::fromJson(api.Tasks("open", "").toUtf8()).array().isEmpty());
        QCOMPARE(QJsonDocument::fromJson(api.Tasks("done", "").toUtf8()).array().size(), 1);
    }

    void dbusCloseOutAndReview()
    {
        ExternalApi api(m_app.get());
        const QString id = m_app->createTask({{"title", "Left over"}, {"time_estimate_minutes", 30}});
        QVERIFY(m_app->scheduleAt(id, m_app->now() - 2 * 3600000LL, 30));
        const QString other = m_app->createTask({{"title", "Not happening"}, {"time_estimate_minutes", 30}});
        QVERIFY(m_app->scheduleAt(other, m_app->now() - 3600000LL, 30));

        QJsonObject day = QJsonDocument::fromJson(api.CloseOut().toUtf8()).object();
        const QJsonArray left = day.value("leftovers").toArray();
        QCOMPARE(left.size(), 2);
        QVERIFY(!day.value("closed").toBool());
        const QJsonObject first = left[0].toObject();
        QCOMPARE(first.value("taskId").toString(), id);
        QVERIFY(!api.ResolveLeftover("block", first.value("id").toString(), "nonsense"));
        QVERIFY(api.ResolveLeftover("block", first.value("id").toString(), "pile"));
        QVERIFY(api.LetGo(other));
        QCOMPARE(m_app->queries().task(other)->status, TaskStatus::Cancelled);
        QVERIFY(QJsonDocument::fromJson(api.CloseOut().toUtf8()).object().value("leftovers").toArray().isEmpty());
        QVERIFY(!api.ClearLeftovers()); // nothing left

        // The day note is added to, never replaced, and each addition undoes on its own.
        m_app->saveDayNote("Typed in the app");
        const QString note = api.AddToDayNote("From an agent");
        QVERIFY(!note.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(api.CloseOut().toUtf8()).object().value("note").toString(),
                 QStringLiteral("Typed in the app\n\nFrom an agent"));
        m_app->undo();
        QCOMPARE(QJsonDocument::fromJson(api.CloseOut().toUtf8()).object().value("note").toString(), QStringLiteral("Typed in the app"));
        QVERIFY(api.AddToDayNote("  ").isEmpty());

        QVERIFY(api.FinishCloseOut());
        QVERIFY(QJsonDocument::fromJson(api.CloseOut().toUtf8()).object().value("closed").toBool());
        const QJsonObject week = QJsonDocument::fromJson(api.WeekReview().toUtf8()).object();
        QVERIFY(week.contains("stale") && week.contains("dueSoon"));
        QVERIFY(!week.value("doneToday").toBool());
        QVERIFY(api.FinishReview());
        QVERIFY(QJsonDocument::fromJson(api.WeekReview().toUtf8()).object().value("doneToday").toBool());

        m_app->setSetting("external_writes", false);
        QVERIFY(api.AddToDayNote("blocked").isEmpty());
        QVERIFY(!api.FinishReview());
        QVERIFY(!api.LetGo(id));
        QVERIFY(!QJsonDocument::fromJson(api.CloseOut().toUtf8()).object().isEmpty()); // reads still work
        m_app->setSetting("external_writes", true);
    }

    void externalApiOverTheSocket()
    {
        ExternalApi api(m_app.get());
        ipc::Server server(&api, [&api] { return api.takeError(); });
        const auto call = [&](const QByteArray &request) { return QJsonDocument::fromJson(server.handle(request)).object(); };

        QJsonObject r = call(R"({"method":"Capture","args":["water plants ~10m"]})");
        QVERIFY(r.value("ok").toBool());
        const QString id = r.value("result").toString();
        QCOMPARE(m_app->queries().task(id)->title, QStringLiteral("water plants"));
        QCOMPARE(m_app->undoText(), QStringLiteral("Capture “water plants”"));

        r = call(R"({"method":"Task","args":["no-such-task"]})");
        QVERIFY(!r.value("ok").toBool());
        QVERIFY(r.value("error").toString().contains("no task with id"));
        QVERIFY(call(R"({"method":"Stats","args":[7]})").value("ok").toBool());

        m_app->setSetting("external_writes", false);
        r = call(R"({"method":"Complete","args":[")" + id.toUtf8() + R"("]})");
        QVERIFY(!r.value("ok").toBool());
        QVERIFY(r.value("error").toString().contains("not let other apps"));
        QVERIFY(call(R"({"method":"Today","args":[]})").value("ok").toBool()); // reads still work
        m_app->setSetting("external_writes", true);
        QVERIFY(call(R"({"method":"Complete","args":[")" + id.toUtf8() + R"("]})").value("result").toBool());
    }

    void focusMode()
    {
        const QString id = m_app->createTask({{"title", "Draft"}});
        QSignalSpy spy(m_app.get(), &AppController::focusModeChanged);
        m_app->startFocus(id);
        QVERIFY(m_app->focusMode());
        QCOMPARE(m_app->working().value("taskId").toString(), id);
        m_app->setFocusMode(false);
        QCOMPARE(spy.count(), 2);
    }

    void keyringOnlyOpenedForClaude()
    {
        Database fresh(QStringLiteral("keys"));
        QVERIFY(fresh.open(QStringLiteral(":memory:")) && fresh.migrate());
        FakeKeyStore keys;
        AppController app(fresh, nullptr, &keys);
        app.startFresh();
        app.capture("anything");
        app.setSetting("ai_provider", "ollama");
        QVERIFY(keys.reads.isEmpty()); // no AI, then Ollama: the wallet stays shut

        app.setSetting("ai_provider", "anthropic");
        QCOMPARE(keys.reads, QStringList({"anthropic-api-key"}));
        QVERIFY(app.ai().value("ready").toBool());
        app.setSetting("ai_model", "claude-opus-5-5");
        QCOMPARE(keys.reads.size(), 1); // read once, not on every settings change
    }

    void remoteRouting()
    {
        QCOMPARE(RemoteHub::route("away", true, true), QStringLiteral("phone"));
        QCOMPARE(RemoteHub::route("away", true, false), QStringLiteral("desktop"));
        QCOMPARE(RemoteHub::route("both", true, false), QStringLiteral("both"));
        QCOMPARE(RemoteHub::route("desktop", true, true), QStringLiteral("desktop"));
        QCOMPARE(RemoteHub::route("both", false, true), QStringLiteral("desktop")); // no phone paired
    }

    void remoteHubSecretsAndCommands()
    {
        QTemporaryDir dir;
        const QString secrets = dir.filePath("remote.json");
        const auto readSecrets = [&] {
            QFile f(secrets);
            if (!f.open(QIODevice::ReadOnly))
                return QJsonObject();
            return QJsonDocument::fromJson(f.readAll()).object();
        };
        QString token, vapid;
        {
            RemoteHub hub(m_app.get(), secrets);
            token = readSecrets().value("token").toString();
            vapid = readSecrets().value("vapidPublic").toString();
            QVERIFY(token.size() >= 40);
            QCOMPARE(QFile(secrets).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther),
                     QFileDevice::Permissions());
            QVERIFY(hub.state().value("pairUrl").toString().endsWith("/#pair=" + token));
            QVERIFY(!hub.state().value("secure").toBool());

            QString error;
            QJsonObject r = hub.command({{"type", "capture"}, {"text", "milk tomorrow"}, {"clientId", "c-1"}}, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
            const QString id = r.value("id").toString();
            QVERIFY(!id.isEmpty());
            // The phone's outbox retries: same clientId, nothing new.
            r = hub.command({{"type", "capture"}, {"text", "milk tomorrow"}, {"clientId", "c-1"}}, &error);
            QVERIFY(r.value("duplicate").toBool());
            QCOMPARE(m_app->externalTasks("open", "milk").size(), 1);

            QVERIFY(hub.command({{"type", "startWork"}, {"taskId", id}}, &error).value("ok").toBool());
            QCOMPARE(hub.snapshot().value("working").toObject().value("taskId").toString(), id);
            QVERIFY(hub.command({{"type", "stopWork"}}, &error).value("ok").toBool());
            QVERIFY(hub.command({{"type", "energy"}, {"level", 6}}, &error).value("ok").toBool());
            QCOMPARE(hub.snapshot().value("energy").toObject().value("level").toInt(), 6);
            QVERIFY(hub.snapshot().value("today").toObject().contains("ready"));

            error.clear();
            hub.command({{"type", "rm -rf"}}, &error);
            QVERIFY(error.contains("unknown"));
            error.clear();
            hub.command({{"type", "completeTask"}, {"taskId", "nope"}}, &error);
            QVERIFY(!error.isEmpty());
            QCOMPARE(hub.preview("call mum friday").value("title").toString(), QStringLiteral("call mum"));
        }
        // A restart keeps the pairing, the push keys and the seen captures.
        RemoteHub again(m_app.get(), secrets);
        QVERIFY(again.state().value("pairUrl").toString().endsWith(token));
        QCOMPARE(QString::fromLatin1(remote::toBase64Url(again.vapidPublicKey())), vapid);
        QString error;
        QVERIFY(again.command({{"type", "capture"}, {"text", "milk tomorrow"}, {"clientId", "c-1"}}, &error).value("duplicate").toBool());

        // Subscriptions persist and can be forgotten; a new code forgets them all.
        remote::Subscription sub;
        sub.endpoint = "https://fcm.googleapis.com/fcm/send/x";
        sub.p256dh = QByteArray(65, '\x04');
        sub.auth = QByteArray(16, 'a');
        sub.label = "Pixel";
        QVERIFY(again.addSubscription(sub));
        QVERIFY(again.hasPhones());
        QCOMPARE(readSecrets().value("subscriptions").toArray().size(), 1);
        again.rotateToken();
        QVERIFY(!again.hasPhones());
        QVERIFY(!again.state().value("pairUrl").toString().endsWith(token));
    }

    void remoteHubClosesOutTheDay()
    {
        QTemporaryDir dir;
        RemoteHub hub(m_app.get(), dir.filePath("remote.json"));
        const QString id = m_app->createTask({{"title", "Left over"}, {"time_estimate_minutes", 30}});
        QVERIFY(m_app->scheduleAt(id, m_app->now() - 2 * 3600000LL, 30));
        const QString done = m_app->createTask({{"title", "Done one"}});
        QVERIFY(m_app->setTaskStatus(done, "completed"));

        QJsonObject day = hub.closeOut();
        QCOMPARE(day.value("wins").toArray().size(), 1);
        const QJsonArray left = day.value("leftovers").toArray();
        QCOMPARE(left.size(), 1);

        QString error;
        QVERIFY(hub.command({{"type", "dayNote"}, {"text", ""}}, &error).value("ok").toBool()); // nothing yet: fine
        QVERIFY(hub.command({{"type", "dayNote"}, {"text", "Tired but OK"}}, &error).value("ok").toBool());
        QCOMPARE(hub.closeOut().value("note").toString(), QStringLiteral("Tired but OK"));

        const QJsonObject l = left[0].toObject();
        QVERIFY(hub.command({{"type", "leftover"}, {"kind", l.value("kind")}, {"id", l.value("id")}, {"choice", "pile"}}, &error).value("ok").toBool());
        QVERIFY(hub.closeOut().value("leftovers").toArray().isEmpty());
        hub.command({{"type", "clearLeftovers"}}, &error); // nothing left to clear
        QVERIFY(!error.isEmpty());

        error.clear();
        QVERIFY(hub.command({{"type", "finishCloseOut"}, {"quietly", true}}, &error).value("ok").toBool());
        QVERIFY(hub.closeOut().value("closed").toBool());
        QVERIFY(hub.snapshot().value("today").toObject().contains("closeOutDue"));
    }

    void remoteHubServesWhenEnabled()
    {
        QTemporaryDir dir;
        RemoteHub hub(m_app.get(), dir.filePath("remote.json"));
        QVERIFY(!hub.state().value("running").toBool());

        // A port that's free right now, rather than a fixed one that might not be.
        quint16 freePort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            freePort = probe.serverPort();
        }
        const QString port = QString::number(freePort);

        m_app->setSetting("remote_listen", "0.0.0.0:" + port);
        m_app->setSetting("remote_enabled", true);
        QVERIFY(!hub.state().value("running").toBool());
        QVERIFY(hub.state().value("error").toString().contains("clear"));

        m_app->setSetting("remote_listen", "127.0.0.1:" + port);
        QVERIFY2(hub.state().value("running").toBool(), qPrintable(hub.state().value("error").toString()));
        const QString pair = hub.state().value("pairUrl").toString();
        QCOMPARE(pair.left(pair.indexOf("/#")), "http://127.0.0.1:" + port);
        const QByteArray token = pair.mid(pair.indexOf("#pair=") + 6).toLatin1();

        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, freePort);
        QTRY_COMPARE(s.state(), QAbstractSocket::ConnectedState);
        s.write("GET /api/snapshot HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token + "\r\n\r\n");
        QByteArray got;
        QTRY_VERIFY((got += s.readAll()).contains("\"today\""));
        QVERIFY(got.startsWith("HTTP/1.1 200"));

        m_app->setSetting("remote_public_url", "https://box.tail1234.ts.net:7789/");
        QVERIFY(hub.state().value("pairUrl").toString().startsWith("https://box.tail1234.ts.net:7789/#pair="));
        QVERIFY(hub.state().value("secure").toBool());
        QCOMPARE(hub.state().value("serveCommand").toString(), QStringLiteral("tailscale serve --bg --https=%1 http://127.0.0.1:%1").arg(port));

        m_app->setSetting("remote_enabled", false);
        QVERIFY(!hub.state().value("running").toBool());
    }

    void syncHubLinearEndToEnd()
    {
        FakeServer server;
        FakeKeyStore keys;
        SyncHub hub(m_app.get(), &keys);
        hub.setLinearEndpoint(server.url());

        server.reply(200, {{"data", QJsonObject{{"viewer", QJsonObject{{"name", "April"}}},
                                                {"teams", QJsonObject{{"nodes", QJsonArray{QJsonObject{{"key", "ENG"}, {"name", "Engineering"}}}}}}}}});
        const QJsonObject issue{{"id", "uuid-1"}, {"title", "Ship it"}, {"priority", 2}, {"url", "https://linear.app/a/issue/ENG-1"},
                                {"state", QJsonObject{{"type", "started"}}}, {"team", QJsonObject{{"key", "ENG"}}},
                                {"labels", QJsonObject{{"nodes", QJsonArray()}}}};
        const QJsonObject page{{"data", QJsonObject{{"issues", QJsonObject{{"nodes", QJsonArray{issue}},
                                                                            {"pageInfo", QJsonObject{{"hasNextPage", false}}}}}}}};
        server.reply(200, page);

        QSignalSpy finished(&hub, &SyncHub::syncFinished);
        hub.connectLinear("  lin_api_123  ");
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(keys.writes, QStringList({"linear-api-key"}));
        QCOMPARE(server.requests.first().headers.value("authorization"), QStringLiteral("lin_api_123"));
        const QVariantMap linear = hub.state().value("linear").toMap();
        QCOMPARE(linear.value("user").toString(), QStringLiteral("April"));
        QVERIFY2(linear.value("error").toString().isEmpty(), qPrintable(linear.value("error").toString()));

        const QVariantList found = m_app->externalTasks("open", "Ship it");
        QCOMPARE(found.size(), 1);
        const QString taskId = found.first().toMap().value("id").toString();
        QCOMPARE(m_app->taskDetail(taskId).value("priority").toString(), QStringLiteral("high"));

        // Done here: the next sync tells Linear (look up states, then update).
        QVERIFY(m_app->setTaskStatus(taskId, "completed"));
        server.reply(200, page); // still open there
        server.reply(200, {{"data", QJsonObject{{"issue", QJsonObject{
                                                              {"state", QJsonObject{{"type", "started"}}},
                                                              {"team", QJsonObject{{"states", QJsonObject{{"nodes", QJsonArray{
                                                                  QJsonObject{{"id", "s-done"}, {"type", "completed"}, {"position", 1}}}}}}}}}}}}});
        server.reply(200, {{"data", QJsonObject{{"issueUpdate", QJsonObject{{"success", true}}}}}});
        hub.syncNow();
        QTRY_COMPARE(server.requests.size(), 5); // viewer, pull, pull, states, update
        QCOMPARE(server.requests.last().body.value("variables").toObject().value("stateId").toString(), QStringLiteral("s-done"));
        QTRY_COMPARE(scalar(*m_db, "SELECT sync_status FROM synced_items").toString(), QStringLiteral("done_pushed"));
        QCOMPARE(m_app->taskDetail(taskId).value("status").toString(), QStringLiteral("completed"));

        hub.disconnectLinear();
        QVERIFY(!hub.state().value("linear").toMap().value("enabled").toBool());
        QCOMPARE(keys.writes.size(), 2); // the key was removed again
    }

    void syncHubCalendarFeed()
    {
        QTemporaryDir dir;
        const QDate tomorrow = QDate::currentDate().addDays(1);
        const QString stamp = tomorrow.toString("yyyyMMdd");
        QFile ics(dir.filePath("work.ics"));
        QVERIFY(ics.open(QIODevice::WriteOnly));
        ics.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nBEGIN:VEVENT\r\nUID:m1\r\nSUMMARY:Planning\r\nDTSTART:" + stamp.toLatin1()
                  + "T140000Z\r\nDTEND:" + stamp.toLatin1() + "T150000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
        ics.close();

        FakeKeyStore keys;
        SyncHub hub(m_app.get(), &keys);
        QSignalSpy added(&hub, &SyncHub::feedAdded);
        QSignalSpy failed(&hub, &SyncHub::feedFailed);
        hub.addFeed({{"name", "Work"}, {"url", ics.fileName()}, {"color", "#ff0000"}, {"blocking", "block_all"}});
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(failed.size(), 0);
        QVERIFY(keys.reads.isEmpty()); // calendars never touch the keyring
        const QString feedId = added.first().first().toString();
        QCOMPARE(hub.state().value("feeds").toList().first().toMap().value("count").toInt(), 1);

        const QString eventId = scalar(*m_db, "SELECT id FROM events WHERE title = 'Planning'").toString();
        QVERIFY(!eventId.isEmpty());
        QCOMPARE(m_app->eventDetail(eventId).value("feed").toString(), QStringLiteral("Work"));
        // Read-only here.
        QVERIFY(m_app->saveEvent({{"id", eventId}, {"title", "Mine now"}}).isEmpty());
        QVERIFY(!m_app->deleteEvent(eventId));

        hub.addFeed({{"url", dir.filePath("nope.ics")}});
        QTRY_COMPARE(failed.size(), 1);

        hub.updateFeed(feedId, {{"enabled", false}});
        QCOMPARE(scalar(*m_db, "SELECT COUNT(*) FROM events").toInt(), 0);
        hub.updateFeed(feedId, {{"enabled", true}});
        QTRY_COMPARE(scalar(*m_db, "SELECT COUNT(*) FROM events").toInt(), 1);
        hub.removeFeed(feedId);
        QCOMPARE(scalar(*m_db, "SELECT COUNT(*) FROM events").toInt(), 0);
        QVERIFY(hub.state().value("feeds").toList().isEmpty());
    }

    void deviceCalendarEvents()
    {
        using namespace devicecalendars;
        QCOMPARE(calendarOf(feedUrl("ABC-123")).value_or(QString()), QStringLiteral("ABC-123"));
        QVERIFY(!calendarOf("https://example.com/cal.ics"));
        QVERIFY(!calendarOf("device:"));

        sync::FeedSettings feed;
        feed.color = "#123456";
        feed.blocking = BlockingMode::RecurringOnly;
        devicecalendars::Occurrence meeting;
        meeting.id = "m@1";
        meeting.title = "Standup";
        meeting.start = 1000;
        meeting.end = 500; // ends before it starts: clamped
        devicecalendars::Occurrence holiday;
        holiday.id = "h@2";
        holiday.allDay = true;
        holiday.startDate = QDate(2026, 12, 24);
        holiday.endDate = QDate(2026, 12, 26);
        holiday.free = true;
        const QList<sync::RemoteEvent> events = toRemoteEvents({meeting, holiday}, feed);
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].externalId, QStringLiteral("h@2")); // sorted by id
        QVERIFY(events[0].event.allDay);
        QCOMPARE(events[0].event.endDate, QDate(2026, 12, 26));
        QCOMPARE(events[0].event.title, QStringLiteral("(busy)"));
        QCOMPARE(events[0].event.blocking.mode, BlockingMode::None); // shown as free
        QCOMPARE(events[1].event.title, QStringLiteral("Standup"));
        QCOMPARE(events[1].event.endAt, qint64(1000));
        QCOMPARE(events[1].event.blocking.mode, BlockingMode::RecurringOnly);
        QCOMPARE(events[1].event.color, QStringLiteral("#123456"));

        // Here (Linux) there's nothing to read: a device feed says so rather than failing quietly.
        FakeKeyStore keys;
        SyncHub hub(m_app.get(), &keys);
        QCOMPARE(hub.deviceCalendars().value("supported").toBool(), supported());
        QJsonArray stored{QJsonObject{{"id", "f1"}, {"name", "Work"}, {"url", feedUrl("X")}, {"enabled", true}}};
        m_app->setSetting("calendar_feeds", stored);
        QSignalSpy done(&hub, &SyncHub::syncFinished);
        hub.syncNow();
        QTRY_COMPARE(done.size(), 1);
        const QVariantMap f = hub.state().value("feeds").toList().first().toMap();
        QVERIFY(f.value("device").toBool());
        if (!supported())
            QVERIFY(!f.value("error").toString().isEmpty());
        hub.removeFeed("f1");
        QVERIFY(hub.state().value("feeds").toList().isEmpty());
    }

    void notesFromTheUi()
    {
        const QString task = m_app->createTask({{"title", "Plan the trip"}});
        const QString id = m_app->createNote({{"title", "Packing <list>"}, {"taskId", task}, {"tags", QStringList{"travel"}}});
        QVERIFY(!id.isEmpty());
        QCOMPARE(m_app->undoText(), QStringLiteral("Add note “Packing <list>”"));

        // Typing saves without piling up undo steps.
        QVERIFY(m_app->saveNote(id, {{"content", "passport & charger"}}));
        QVERIFY(m_app->saveNote(id, {{"content", "passport & charger, adapter"}}));
        QCOMPARE(m_app->undoText(), QStringLiteral("Add note “Packing <list>”"));

        const QVariantList hits = m_app->notes("charg");
        QCOMPARE(hits.size(), 1);
        // The user's text is escaped; only the match markers become markup.
        QCOMPARE(hits.first().toMap().value("snippet").toString(), QStringLiteral("passport &amp; <b>charger</b>, adapter"));
        QCOMPARE(m_app->notes().first().toMap().value("title").toString(), QStringLiteral("Packing <list>"));
        QCOMPARE(m_app->notes({}, {}, task).size(), 1);
        QCOMPARE(m_app->noteDetail(id).value("taskTitle").toString(), QStringLiteral("Plan the trip"));
        QCOMPARE(m_app->noteTags(), QStringList({"travel"}));
        QVERIFY(m_app->noteTaskChoices().size() >= 1);

        QTemporaryDir dir;
        QVERIFY(m_app->exportNote(id, QUrl::fromLocalFile(dir.filePath("packing.md"))));
        QFile f(dir.filePath("packing.md"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("# Packing <list>\n\nTags: travel\n\npassport & charger, adapter"));

        QVERIFY(m_app->deleteNote(id));
        QVERIFY(m_app->notes().isEmpty());
        m_app->undo();
        QCOMPARE(m_app->noteDetail(id).value("content").toString(), QStringLiteral("passport & charger, adapter"));
        QCOMPARE(m_app->notes("adapter").size(), 1);
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
