#include "appcontroller.h"
#include "desktop/dbusapi.h"
#include "keystore.h"
#include "remotehub.h"
#include "fakeserver.h"
#include "tasklistmodel.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTest>
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

    void dbusApiAndWriteSwitch()
    {
        DBusApi api(m_app.get());
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
