// Real models, not fakes: skipped unless asked for, and not part of CI.
//   TUMBLER_LIVE_OLLAMA=model[,model...] [TUMBLER_LIVE_OLLAMA_URL=http://localhost:11434]
//   TUMBLER_LIVE_CLAUDE_CLI=1 [TUMBLER_LIVE_CLAUDE_MODEL=haiku]
//   TUMBLER_LIVE_RUNS=3 (each feature, each model)
// Prints one line per call: model, feature, ok or the error, and seconds.

#include "aifeatures.h"
#include "jsonschema.h"
#include "provider.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QTest>

using namespace tumbler;
using namespace tumbler::ai;

namespace {

Task task(const QString &id, const QString &title, int minutes, Energy energy, Priority priority, const QString &description = {})
{
    Task t;
    t.id = id;
    t.title = title;
    t.estimateMinutes = minutes;
    t.energy = energy;
    t.priority = priority;
    t.description = description;
    return t;
}

PlanContext planContext()
{
    PlanContext c;
    c.day = QDate::currentDate();
    c.nowText = QLocale::c().toString(QDateTime::currentDateTime(), QStringLiteral("dddd d MMMM, HH:mm"));
    c.energy = 5;
    c.peakHours = QStringLiteral("09:00-12:00");
    c.userHints = QStringLiteral("I focus best before lunch. Keep calls in the afternoon.");
    c.tasks = {
        task("t1", "Draft the quarterly report", 90, Energy::High, Priority::High, "Sections: revenue, hiring, risks. Due to Sam."),
        task("t2", "Renew passport", 20, Energy::Low, Priority::Medium, "Form is on the gov site; need a new photo."),
        task("t3", "Call the dentist", 10, Energy::Low, Priority::Medium),
        task("t4", "Review Alex's pull request", 45, Energy::High, Priority::High, "The sync refactor, about 600 lines."),
        task("t5", "Water the plants", 5, Energy::Low, Priority::Low),
        task("t6", "Plan the team offsite", 60, Energy::Medium, Priority::Low, "Venue shortlist in the shared doc."),
        task("t7", "Reply to the landlord", 15, Energy::Medium, Priority::Medium, "About the boiler service date."),
        task("t8", "Book flights for the offsite", 30, Energy::Medium, Priority::Low),
    };
    c.tasks[0].dueDate = c.day.addDays(1);
    c.tasks[0].tags = {QStringLiteral("work")};
    c.dependencies = {{QStringLiteral("t8"), QStringLiteral("t6")}};
    c.fixedToday = {QStringLiteral("13:00-14:00 Lunch with Jo (event)")};
    c.includeNotes = true; // the case that used to confuse local models
    return c;
}

std::vector<std::unique_ptr<Provider>> providers(QNetworkAccessManager *net)
{
    std::vector<std::unique_ptr<Provider>> out;
    const QUrl url(qEnvironmentVariable("TUMBLER_LIVE_OLLAMA_URL", QStringLiteral("http://localhost:11434")));
    for (const QString &m : qEnvironmentVariable("TUMBLER_LIVE_OLLAMA").split(QLatin1Char(','), Qt::SkipEmptyParts))
        out.push_back(std::make_unique<OllamaProvider>(net, url, m.trimmed()));
    if (qEnvironmentVariableIsSet("TUMBLER_LIVE_CLAUDE_CLI")) {
        const QString program = ClaudeCliProvider::locate(QString());
        if (program.isEmpty())
            qWarning("TUMBLER_LIVE_CLAUDE_CLI: claude isn't installed");
        else
            out.push_back(std::make_unique<ClaudeCliProvider>(program, qEnvironmentVariable("TUMBLER_LIVE_CLAUDE_MODEL")));
    }
    return out;
}

Result run(Provider *p, const Request &r)
{
    Result out;
    bool done = false;
    runValidated(p, r, [&](const Result &res) {
        out = res;
        done = true;
    });
    if (!QTest::qWaitFor([&] { return done; }, 10 * 60 * 1000))
        out.error = QStringLiteral("no answer in 10 minutes");
    return out;
}

} // namespace

class TestAiLive : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager m_net;

private slots:
    void features()
    {
        auto list = providers(&m_net);
        if (list.empty())
            QSKIP("set TUMBLER_LIVE_OLLAMA and/or TUMBLER_LIVE_CLAUDE_CLI to run against real models");
        const int runs = std::max(1, qEnvironmentVariableIntValue("TUMBLER_LIVE_RUNS"));

        const PlanContext plan = planContext();
        QSet<QString> ids;
        for (const Task &t : plan.tasks)
            ids.insert(t.id);
        const QList<QPair<QString, Request>> requests{
            {QStringLiteral("plan"), planRequest(plan)},
            {QStringLiteral("breakdown"), breakdownRequest(plan.tasks[0], {}, true, {})},
            {QStringLiteral("capture"), captureRequest(QStringLiteral("dentist next tues 3pm, takes like half an hour"), plan.day,
                                                       {QStringLiteral("Work"), QStringLiteral("Home")})},
        };

        int failures = 0;
        for (const auto &p : list) {
            for (const auto &[name, request] : requests) {
                for (int i = 0; i < runs; ++i) {
                    const Result r = run(p.get(), request);
                    QString detail;
                    if (r.ok && name == QLatin1String("plan")) {
                        const PlanGuidance g = toGuidance(r.data, ids);
                        detail = QStringLiteral("%1 ordered, %2 deferred, %3 notes").arg(g.order.size()).arg(g.defer.size()).arg(g.notes.size());
                        if (g.order.isEmpty()) {
                            detail += QStringLiteral(" (EMPTY PLAN)");
                            ++failures;
                        }
                    } else if (r.ok && name == QLatin1String("breakdown")) {
                        detail = QStringLiteral("%1 steps").arg(toSteps(r.data).size());
                    } else if (r.ok) {
                        detail = toCapture(r.data, plan.day).title;
                    } else {
                        detail = r.error;
                        ++failures;
                    }
                    qInfo().noquote() << QStringLiteral("%1 %2 %3: %4 (%5 s)")
                                             .arg(p->model().isEmpty() ? p->id() : p->model(), name, r.ok ? QStringLiteral("ok") : QStringLiteral("FAILED"),
                                                  detail)
                                             .arg(r.elapsedMs / 1000.0, 0, 'f', 1);
                }
            }
        }
        QCOMPARE(failures, 0);
    }
};

int main(int argc, char **argv)
{
    // Real models are slow; QtTest's default limit is five minutes a test.
    if (qEnvironmentVariableIsEmpty("QTEST_FUNCTION_TIMEOUT"))
        qputenv("QTEST_FUNCTION_TIMEOUT", "86400000");
    QCoreApplication app(argc, argv);
    TestAiLive test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_ai_live.moc"
