#include "aifeatures.h"
#include "jsonschema.h"
#include "provider.h"
#include "fakeserver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace tumbler;
using namespace tumbler::ai;

namespace {

QJsonObject claudeText(const QString &text, const QString &stop = QStringLiteral("end_turn"))
{
    return {{"type", "message"},
            {"model", "claude-opus-5-5"},
            {"stop_reason", stop},
            {"content", QJsonArray{QJsonObject{{"type", "thinking"}, {"thinking", ""}}, QJsonObject{{"type", "text"}, {"text", text}}}}};
}

Request simpleRequest()
{
    Request r;
    r.feature = "test";
    r.system = "sys";
    r.prompt = "hello";
    r.schema = QJsonObject{{"type", "object"},
                           {"properties", QJsonObject{{"answer", QJsonObject{{"type", "integer"}}}}},
                           {"required", QJsonArray{"answer"}},
                           {"additionalProperties", false}};
    return r;
}

Result await(Provider &p, const Request &r, bool validated = false)
{
    Result out;
    bool done = false;
    auto cb = [&](const Result &res) {
        out = res;
        done = true;
    };
    if (validated)
        runValidated(&p, r, cb);
    else
        p.send(r, cb);
    if (!QTest::qWaitFor([&] { return done; }, 5000))
        out.error = QStringLiteral("test timed out waiting for the provider");
    return out;
}

} // namespace

class TestAi : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager m_net;

private slots:
    void schemaValidation()
    {
        const QJsonObject schema = simpleRequest().schema;
        QVERIFY(validate(QJsonObject{{"answer", 3}}, schema).isEmpty());
        QCOMPARE(validate(QJsonObject{}, schema), QStringLiteral("$.answer: missing"));
        QCOMPARE(validate(QJsonObject{{"answer", 3.5}}, schema), QStringLiteral("$.answer: expected integer"));
        QCOMPARE(validate(QJsonObject{{"answer", 1}, {"extra", 1}}, schema), QStringLiteral("$.extra: not allowed"));

        const QJsonObject steps = breakdownRequest(Task{}, {}, false, {}).schema;
        const QJsonObject bad{{"steps", QJsonArray{QJsonObject{{"title", "a"}, {"minutes", 5}, {"energy", "extreme"}}}}, {"explanation", ""}};
        QCOMPARE(validate(bad, steps), QStringLiteral("$.steps[0].energy: not one of the allowed values"));
    }

    void anthropicRequestShape()
    {
        FakeServer server;
        server.reply(200, claudeText("{\"answer\": 42}"));
        AnthropicProvider claude(&m_net, "sk-test");
        claude.setBaseUrl(server.url());
        const Result r = await(claude, simpleRequest());

        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.data.value("answer").toInt(), 42);
        QCOMPARE(r.provider, QStringLiteral("anthropic"));

        const auto &seen = server.requests.first();
        QCOMPARE(seen.path, QStringLiteral("/v1/messages"));
        QCOMPARE(seen.headers.value("x-api-key"), QStringLiteral("sk-test"));
        QCOMPARE(seen.headers.value("anthropic-version"), QStringLiteral("2023-06-01"));
        QCOMPARE(seen.headers.value("anthropic-beta"), QStringLiteral("server-side-fallback-2026-07-01"));
        QCOMPARE(seen.body.value("model").toString(), QStringLiteral("claude-opus-5-5"));
        QCOMPARE(seen.body.value("fallbacks").toString(), QStringLiteral("default"));
        QCOMPARE(seen.body.value("system").toString(), QStringLiteral("sys"));
        const QJsonObject config = seen.body.value("output_config").toObject();
        QCOMPARE(config.value("effort").toString(), QStringLiteral("medium"));
        QCOMPARE(config.value("format").toObject().value("type").toString(), QStringLiteral("json_schema"));
        QCOMPARE(config.value("format").toObject().value("schema").toObject(), simpleRequest().schema);
        QVERIFY(!seen.body.contains("thinking")); // left to the model's default
    }

    void anthropicFailures_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QJsonObject>("body");
        QTest::addColumn<QString>("expected");
        QTest::newRow("refusal") << 200 << claudeText("{}", "refusal") << "declined";
        QTest::newRow("cut off") << 200 << claudeText("{\"ans", "max_tokens") << "cut off";
        QTest::newRow("bad key") << 401 << QJsonObject{{"type", "error"}, {"error", QJsonObject{{"type", "authentication_error"}, {"message", "invalid x-api-key"}}}}
                                 << "rejected the API key";
        QTest::newRow("overloaded") << 529 << QJsonObject{{"type", "error"}, {"error", QJsonObject{{"message", "Overloaded"}}}} << "having trouble";
        QTest::newRow("not json") << 200 << claudeText("sure! here you go") << "wasn't valid JSON";
        QTest::newRow("no text") << 200 << QJsonObject{{"stop_reason", "end_turn"}, {"content", QJsonArray{}}} << "no text";
    }

    void anthropicFailures()
    {
        QFETCH(int, status);
        QFETCH(QJsonObject, body);
        QFETCH(QString, expected);
        FakeServer server;
        server.reply(status, body);
        AnthropicProvider claude(&m_net, "sk-test");
        claude.setBaseUrl(server.url());
        const Result r = await(claude, simpleRequest());
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(expected), qPrintable(r.error));
    }

    void unreachable()
    {
        AnthropicProvider claude(&m_net, "sk-test");
        claude.setBaseUrl(QUrl("http://127.0.0.1:1"));
        const Result r = await(claude, simpleRequest());
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains("couldn't reach"), qPrintable(r.error));
    }

    void validatedRetryOnce()
    {
        FakeServer server;
        server.reply(200, claudeText("{\"answer\": \"forty-two\"}"));
        server.reply(200, claudeText("{\"answer\": 42}"));
        AnthropicProvider claude(&m_net, "sk-test");
        claude.setBaseUrl(server.url());
        const Result r = await(claude, simpleRequest(), true);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(server.requests.size(), 2);
        const QString retryPrompt = server.requests[1].body.value("messages").toArray().first().toObject().value("content").toString();
        QVERIFY(retryPrompt.contains("$.answer: expected integer"));

        // Two bad answers in a row: give up with the reason.
        FakeServer stubborn;
        stubborn.reply(200, claudeText("{\"answer\": \"no\"}"));
        stubborn.reply(200, claudeText("{\"answer\": \"still no\"}"));
        claude.setBaseUrl(stubborn.url());
        const Result failed = await(claude, simpleRequest(), true);
        QVERIFY(!failed.ok);
        QVERIFY(failed.error.contains("expected shape"));
        QCOMPARE(stubborn.requests.size(), 2);
    }

    void ollama()
    {
        FakeServer server;
        server.reply(200, QJsonObject{{"model", "qwen3:8b"}, {"message", QJsonObject{{"role", "assistant"}, {"content", "{\"answer\": 7}"}}}});
        OllamaProvider local(&m_net, server.url(), "qwen3:8b");
        const Result r = await(local, simpleRequest());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.data.value("answer").toInt(), 7);
        const auto &seen = server.requests.first();
        QCOMPARE(seen.path, QStringLiteral("/api/chat"));
        QCOMPARE(seen.body.value("stream").toBool(true), false);
        QCOMPARE(seen.body.value("format").toObject(), simpleRequest().schema);
        QCOMPARE(seen.body.value("messages").toArray().size(), 2);

        server.reply(404, QJsonObject{{"error", "model \"nope\" not found, try pulling it first"}});
        OllamaProvider missing(&m_net, server.url(), "nope");
        const Result err = await(missing, simpleRequest());
        QVERIFY(!err.ok);
        QVERIFY2(err.error.contains("not found"), qPrintable(err.error));

        server.reply(200, QJsonObject{{"models", QJsonArray{QJsonObject{{"name", "qwen3:8b"}}, QJsonObject{{"name", "gemma3:4b"}}}}});
        QStringList models;
        bool done = false;
        OllamaProvider::listModels(&m_net, server.url(), [&](const QStringList &m, const QString &) {
            models = m;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(models, QStringList({"gemma3:4b", "qwen3:8b"}));
    }

    void extractsJsonFromMessyAnswers_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ok");
        QTest::newRow("clean") << "{\"answer\": 1}" << true;
        QTest::newRow("fenced") << "```json\n{\"answer\": 1}\n```" << true;
        QTest::newRow("think block") << "<think>The user wants {a number}.</think>\n{\"answer\": 1}" << true;
        QTest::newRow("chatty") << "Sure! Here it is: {\"answer\": 1} Hope that helps." << true;
        QTest::newRow("braces in strings") << "Result: {\"answer\": 1, \"note\": \"use } and { freely \\\" ok\"} done" << true;
        QTest::newRow("truncated") << "{\"answer\": 1, \"note\": \"cut" << false;
        QTest::newRow("prose") << "I can't do that." << false;
        QTest::newRow("array") << "[1, 2]" << false;
    }

    void extractsJsonFromMessyAnswers()
    {
        QFETCH(QString, text);
        QFETCH(bool, ok);
        QString error;
        const QJsonObject o = extractJsonObject(text, &error);
        QCOMPARE(error.isEmpty(), ok);
        if (ok)
            QCOMPARE(o.value("answer").toInt(), 1);
    }

    void ollamaLimitsAndCutOffs()
    {
        FakeServer server;
        server.reply(200, QJsonObject{{"done_reason", "length"}, {"message", QJsonObject{{"content", "{\"answer\": "}}}});
        OllamaProvider local(&m_net, server.url(), "m");
        Request req = simpleRequest();
        req.maxTokens = 3000;
        const Result r = await(local, req);
        QVERIFY(!r.ok);
        QVERIFY(r.retryable);
        QVERIFY(r.error.contains("cut off"));
        const QJsonObject options = server.requests.first().body.value("options").toObject();
        QCOMPARE(options.value("num_ctx").toInt(), 16384);
        QCOMPARE(options.value("num_predict").toInt(), 3000);
    }

    void malformedAnswerRetriedOnce()
    {
        FakeServer server;
        const auto answer = [](const QString &content) { return QJsonObject{{"message", QJsonObject{{"content", content}}}}; };
        server.reply(200, answer("{\"answer\": 4"));          // broken
        server.reply(200, answer("```json\n{\"answer\": 4}\n```")); // fine once unwrapped
        OllamaProvider local(&m_net, server.url(), "m");
        const Result r = await(local, simpleRequest(), true);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.data.value("answer").toInt(), 4);
        QCOMPARE(server.requests.size(), 2);
        const QString retry = server.requests[1].body.value("messages").toArray().last().toObject().value("content").toString();
        QVERIFY(retry.contains("wasn't usable"));

        // Broken twice: give up after one retry.
        FakeServer stubborn;
        stubborn.reply(200, answer("nope"));
        stubborn.reply(200, answer("still nope"));
        OllamaProvider other(&m_net, stubborn.url(), "m");
        const Result failed = await(other, simpleRequest(), true);
        QVERIFY(!failed.ok);
        QCOMPARE(stubborn.requests.size(), 2);
    }

    void planPromptAndGuidance()
    {
        Task a;
        a.id = "a";
        a.title = "Report";
        a.description = "secret client details";
        Task b;
        b.id = "b";
        b.title = "Email";
        PlanContext c;
        c.day = QDate(2026, 10, 26);
        c.nowText = "Monday 26 October, 08:00";
        c.energy = 4;
        c.tasks = {a, b};
        c.dependencies = {{"a", "b"}};
        Request r = planRequest(c);
        QVERIFY(r.prompt.contains("\"id\": \"a\""));
        QVERIFY(r.prompt.contains("4/10"));
        QVERIFY(r.prompt.contains("waits_on"));
        QVERIFY(!r.prompt.contains("secret client details")); // notes stay local by default
        c.includeNotes = true;
        QVERIFY(planRequest(c).prompt.contains("secret client details"));

        const QJsonObject answer{
            {"order", QJsonArray{"b", "ghost", "a", "b"}},
            {"defer", QJsonArray{QJsonObject{{"task_id", "a"}, {"reason", "dup"}}, QJsonObject{{"task_id", "zzz"}, {"reason", "?"}}}},
            {"notes", QJsonArray{QJsonObject{{"task_id", "b"}, {"note", "quick win"}}}},
            {"explanation", "Start small."}};
        QVERIFY(validate(answer, r.schema).isEmpty());
        const PlanGuidance g = toGuidance(answer, {"a", "b"});
        QCOMPARE(g.order, QStringList({"b", "a"})); // invented ids and duplicates dropped
        QVERIFY(g.defer.isEmpty());                // "a" is ordered, so not deferred
        QCOMPARE(g.notes.value("b"), QStringLiteral("quick win"));
        QCOMPARE(g.explanation, QStringLiteral("Start small."));
    }

    void stepsAndCapture()
    {
        const QJsonObject steps{{"steps", QJsonArray{QJsonObject{{"title", " Open the doc "}, {"minutes", 1}, {"energy", "low"}},
                                                     QJsonObject{{"title", ""}, {"minutes", 10}, {"energy", "low"}},
                                                     QJsonObject{{"title", "Write"}, {"minutes", 900}, {"energy", "peak"}}}},
                                {"explanation", ""}};
        const QList<Step> s = toSteps(steps);
        QCOMPARE(s.size(), 2);
        QCOMPARE(s[0].title, QStringLiteral("Open the doc"));
        QCOMPARE(s[0].minutes, 5);
        QCOMPARE(s[1].minutes, 480);
        QCOMPARE(s[1].energy, Energy::Peak);

        const QDate today(2026, 10, 26);
        const QJsonObject capture{{"title", "Renew passport"},
                                  {"priority", "high"},
                                  {"energy", "low"},
                                  {"estimate_minutes", 45},
                                  {"date", "2026-11-02"},
                                  {"time", "14:30"},
                                  {"tags", QJsonArray{"#admin", "admin", "travel"}},
                                  {"category", "Home"},
                                  {"recurrence", QJsonObject{{"type", "none"}, {"interval", 1}, {"days", QJsonArray{}}}}};
        QVERIFY(validate(capture, captureRequest("x", today, {}).schema).isEmpty());
        const CaptureResult c = toCapture(capture, today);
        QCOMPARE(c.title, QStringLiteral("Renew passport"));
        QCOMPARE(c.date, std::optional<QDate>(QDate(2026, 11, 2)));
        QCOMPARE(c.time, std::optional<QTime>(QTime(14, 30)));
        QCOMPARE(c.tags, QStringList({"admin", "travel"}));
        QVERIFY(!c.recurrence);

        QJsonObject past = capture;
        past["date"] = "2019-01-01";
        past["recurrence"] = QJsonObject{{"type", "weekly"}, {"interval", 2}, {"days", QJsonArray{9}}};
        const CaptureResult p = toCapture(past, today);
        QVERIFY(!p.date);       // a past date is a misreading
        QVERIFY(!p.recurrence); // weekday 9 doesn't exist
        past["recurrence"] = QJsonObject{{"type", "weekly"}, {"interval", 1}, {"days", QJsonArray{}}};
        QCOMPARE(toCapture(past, today).recurrence->days, QList<int>({1})); // defaults to today's weekday
    }
};

QTEST_GUILESS_MAIN(TestAi)
#include "tst_ai.moc"
