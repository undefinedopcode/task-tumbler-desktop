#include "ipc.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using namespace tumbler::ipc;

namespace {

class Target : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    Q_SCRIPTABLE QString Echo(const QString &text) { return text; }
    Q_SCRIPTABLE QString Join(const QString &a, int n, bool flag) { return QStringLiteral("%1/%2/%3").arg(a).arg(n).arg(flag); }
    Q_SCRIPTABLE bool Refuse()
    {
        error = QStringLiteral("not allowed");
        return false;
    }
    Q_SCRIPTABLE void Poke() { ++pokes; }
    QString Hidden() { return QStringLiteral("not scriptable"); }

public:
    QString error;
    int pokes = 0;
};

QJsonObject reply(Server &server, const QByteArray &request)
{
    return QJsonDocument::fromJson(server.handle(request)).object();
}

} // namespace

class TestIpc : public QObject
{
    Q_OBJECT

private slots:
    void socketPathPerDatabase()
    {
        const QString usual = socketPath();
        QVERIFY(usual.endsWith(QStringLiteral("/task-tumbler.sock")));
        QTemporaryDir dir;
        const QString a = socketPath(dir.filePath("a.db"));
        QVERIFY(a != usual);
        QCOMPARE(socketPath(dir.filePath("a.db")), a);
        QVERIFY(socketPath(dir.filePath("b.db")) != a);
        QVERIFY(a.size() < 100); // Unix socket paths are short (104 bytes on macOS)
    }

    void callsScriptableSlotsByName()
    {
        Target target;
        Server server(&target, [&target] { return std::exchange(target.error, QString()); });

        QJsonObject r = reply(server, R"({"method":"Echo","args":["hi"]})");
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("result").toString(), QStringLiteral("hi"));

        r = reply(server, R"({"method":"Join","args":["x", 7, true]})");
        QCOMPARE(r.value("result").toString(), QStringLiteral("x/7/1"));
        r = reply(server, R"({"method":"Join","args":[null, 2.0, false]})"); // null is the default
        QCOMPARE(r.value("result").toString(), QStringLiteral("/2/0"));

        r = reply(server, R"({"method":"Poke","args":[]})");
        QVERIFY(r.value("ok").toBool());
        QVERIFY(r.value("result").isNull());
        QCOMPARE(target.pokes, 1);
    }

    void refusesWhatIsNotThere()
    {
        Target target;
        Server server(&target, [&target] { return std::exchange(target.error, QString()); });
        QVERIFY(!reply(server, R"({"method":"Hidden","args":[]})").value("ok").toBool());
        QVERIFY(!reply(server, R"({"method":"deleteLater","args":[]})").value("ok").toBool());
        QVERIFY(!reply(server, R"({"method":"Echo","args":[]})").value("ok").toBool()); // wrong count
        QVERIFY(!reply(server, R"({"method":"Join","args":["x", "seven", true]})").value("ok").toBool());
        QVERIFY(!reply(server, "{nope").value("ok").toBool());
        QVERIFY(!reply(server, "[1,2]").value("ok").toBool());
    }

    void reportsTheTargetsErrors()
    {
        Target target;
        Server server(&target, [&target] { return std::exchange(target.error, QString()); });
        const QJsonObject r = reply(server, R"({"method":"Refuse","args":[]})");
        QVERIFY(!r.value("ok").toBool());
        QCOMPARE(r.value("error").toString(), QStringLiteral("not allowed"));

        // An error left over from elsewhere isn't blamed on the next call.
        target.error = QStringLiteral("stale");
        QVERIFY(reply(server, R"({"method":"Echo","args":["ok"]})").value("ok").toBool());
    }

    void activateIsTheServers()
    {
        Target target;
        Server server(&target, {});
        QSignalSpy spy(&server, &Server::activateRequested);
        QVERIFY(reply(server, R"({"method":"Activate","args":["task-tumbler","--capture"]})").value("ok").toBool());
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy[0][0].toStringList(), QStringList({"task-tumbler", "--capture"}));
    }

    void overTheSocket()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("t.sock");
        Target target;
        Server server(&target, [&target] { return std::exchange(target.error, QString()); });
        QString error;
        QVERIFY2(server.listen(path, &error), qPrintable(error));

        // The client blocks, so it runs on a thread of its own while this one serves.
        QVariant echoed, refused;
        QString echoError, refuseError;
        std::unique_ptr<QThread> thread(QThread::create([&] {
            Client client(path);
            echoed = client.call("Echo", {"over the wire"}, &echoError);
            refused = client.call("Refuse", {}, &refuseError); // same connection
        }));
        thread->start();
        QTRY_VERIFY(thread->isFinished());
        QVERIFY(echoError.isEmpty());
        QCOMPARE(echoed.toString(), QStringLiteral("over the wire"));
        QVERIFY(!refused.isValid());
        QCOMPARE(refuseError, QStringLiteral("not allowed"));

        // A second server can't take over a live socket.
        Server other(&target, {});
        QVERIFY(!other.listen(path, &error));
        QVERIFY(error.contains("another instance"));
    }

    void takesOverAStaleSocket()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("stale.sock");
        Target target;
        {
            // A crashed instance leaves its socket file behind.
            Server dead(&target, {});
            QVERIFY(dead.listen(path));
        }
        QFile stale(path);
        QVERIFY(stale.open(QIODevice::WriteOnly)); // stand-in for the leftover file
        stale.close();
        Server server(&target, {});
        QString error;
        QVERIFY2(server.listen(path, &error), qPrintable(error));
    }

    void clientWithNothingListening()
    {
        QTemporaryDir dir;
        Client client(dir.filePath("nobody.sock"));
        QVERIFY(!client.connect(100));
        QString error;
        QVERIFY(!client.call("Echo", {"x"}, &error).isValid());
        QVERIFY(error.contains("isn't running"));
    }
};

QTEST_GUILESS_MAIN(TestIpc)
#include "tst_ipc.moc"
