#include "mcpserver.h"

#include <QJsonDocument>
#include <QTest>

using namespace tumbler::mcp;

namespace {

class FakeBackend : public Backend
{
public:
    QVariant call(const QString &method, const QVariantList &args, QString *error) override
    {
        calls.append({method, args});
        if (!failWith.isEmpty()) {
            *error = failWith;
            return {};
        }
        return answers.value(method);
    }

    QList<QPair<QString, QVariantList>> calls;
    QHash<QString, QVariant> answers;
    QString failWith;
};

QJsonObject request(const QString &method, const QJsonObject &params = {}, int id = 1)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

QString toolTextOf(const QJsonObject &reply)
{
    return reply.value("result").toObject().value("content").toArray().first().toObject().value("text").toString();
}

bool isToolError(const QJsonObject &reply)
{
    return reply.value("result").toObject().value("isError").toBool();
}

} // namespace

class TestMcp : public QObject
{
    Q_OBJECT

private slots:
    void initializeNegotiatesVersion()
    {
        FakeBackend backend;
        Server server(&backend);
        auto reply = server.handle(request("initialize", {{"protocolVersion", "2025-06-18"}, {"capabilities", QJsonObject{}},
                                                          {"clientInfo", QJsonObject{{"name", "claude-code"}}}}));
        QVERIFY(reply);
        const QJsonObject result = reply->value("result").toObject();
        QCOMPARE(result.value("protocolVersion").toString(), QStringLiteral("2025-06-18"));
        QVERIFY(result.value("capabilities").toObject().contains("tools"));
        QCOMPARE(result.value("serverInfo").toObject().value("name").toString(), QStringLiteral("task-tumbler"));
        QVERIFY(!result.value("instructions").toString().isEmpty());

        // An unknown version gets our newest.
        reply = server.handle(request("initialize", {{"protocolVersion", "1999-01-01"}}));
        QCOMPARE(reply->value("result").toObject().value("protocolVersion").toString(), Server::supportedVersions().first());
    }

    void notificationsGetNoReply()
    {
        FakeBackend backend;
        Server server(&backend);
        QVERIFY(!server.handle({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}));
        QVERIFY(!server.handleLine("   "));
        QVERIFY(server.handle(request("ping")));
    }

    void errors()
    {
        FakeBackend backend;
        Server server(&backend);
        const auto parse = server.handleLine("{not json");
        QCOMPARE(parse->value("error").toObject().value("code").toInt(), -32700);
        QVERIFY(parse->value("id").isNull());
        const auto unknown = server.handle(request("resources/list", {}, 7));
        QCOMPARE(unknown->value("error").toObject().value("code").toInt(), -32601);
        QCOMPARE(unknown->value("id").toInt(), 7);
    }

    void toolsList()
    {
        FakeBackend backend;
        Server server(&backend);
        const QJsonArray tools = server.handle(request("tools/list"))->value("result").toObject().value("tools").toArray();
        QStringList names;
        for (const QJsonValue &t : tools) {
            names.append(t.toObject().value("name").toString());
            QCOMPARE(t.toObject().value("inputSchema").toObject().value("type").toString(), QStringLiteral("object"));
            QVERIFY(!t.toObject().value("description").toString().isEmpty());
        }
        QCOMPARE(names, QStringList({"get_today", "list_tasks", "get_task", "get_current", "add_task", "complete_task", "start_work",
                                     "stop_work", "schedule_task"}));
        // Reads are marked read-only so clients can allow them without asking.
        QVERIFY(tools.first().toObject().value("annotations").toObject().value("readOnlyHint").toBool());
        QVERIFY(!tools.at(4).toObject().value("annotations").toObject().value("readOnlyHint").toBool());
    }

    void toolCallsMapToTheDBusApi()
    {
        FakeBackend backend;
        backend.answers = {{"Capture", "task-123"}, {"Tasks", "[{\"id\":\"a\"}]"}, {"Complete", true}, {"StartWork", false}};
        Server server(&backend);

        auto reply = server.handle(request("tools/call", {{"name", "add_task"}, {"arguments", QJsonObject{{"text", "call mum friday"}}}}));
        QCOMPARE(toolTextOf(*reply), QStringLiteral("{\"id\":\"task-123\"}"));
        QCOMPARE(backend.calls.last(), qMakePair(QStringLiteral("Capture"), QVariantList{"call mum friday"}));

        reply = server.handle(request("tools/call", {{"name", "list_tasks"}, {"arguments", QJsonObject{{"search", "mum"}}}}));
        QCOMPARE(toolTextOf(*reply), QStringLiteral("[{\"id\":\"a\"}]"));
        QCOMPARE(backend.calls.last().second, QVariantList({"open", "mum"}));

        reply = server.handle(request("tools/call", {{"name", "complete_task"}, {"arguments", QJsonObject{{"id", "a"}}}}));
        QVERIFY(!isToolError(*reply));
        reply = server.handle(request("tools/call", {{"name", "start_work"}, {"arguments", QJsonObject{{"id", "zzz"}}}}));
        QVERIFY(isToolError(*reply)); // backend said false
    }

    void toolCallValidationAndFailures()
    {
        FakeBackend backend;
        Server server(&backend);
        QVERIFY(isToolError(*server.handle(request("tools/call", {{"name", "complete_task"}, {"arguments", QJsonObject{}}}))));
        QVERIFY(isToolError(*server.handle(request("tools/call", {{"name", "add_task"}, {"arguments", QJsonObject{{"text", "  "}}}}))));
        QVERIFY(isToolError(*server.handle(request("tools/call", {{"name", "rm_rf"}, {"arguments", QJsonObject{}}}))));
        QVERIFY(backend.calls.isEmpty()); // none of those reached the app

        backend.failWith = "Task Tumbler is set to not let other apps change tasks";
        const auto reply = server.handle(request("tools/call", {{"name", "stop_work"}, {"arguments", QJsonObject{}}}));
        QVERIFY(isToolError(*reply));
        QVERIFY(toolTextOf(*reply).contains("not let other apps"));
    }
};

QTEST_GUILESS_MAIN(TestMcp)
#include "tst_mcp.moc"
