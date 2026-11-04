#include "fakeserver.h"
#include "icsfeed.h"
#include "linear.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

using namespace tumbler;
using namespace tumbler::sync;

namespace {

// Weekly standup with one week skipped and one moved; an all-day trip; a
// "free" focus block; a cancelled meeting; a private appointment; a
// New York call. Times are UTC unless a zone is named.
const QByteArray kIcs =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//test//EN\r\n"
    "BEGIN:VTIMEZONE\r\nTZID:America/New_York\r\n"
    "BEGIN:STANDARD\r\nDTSTART:20071104T020000\r\nRRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU\r\nTZOFFSETFROM:-0400\r\nTZOFFSETTO:-0500\r\nEND:STANDARD\r\n"
    "BEGIN:DAYLIGHT\r\nDTSTART:20070311T020000\r\nRRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\nEND:DAYLIGHT\r\n"
    "END:VTIMEZONE\r\n"
    "BEGIN:VEVENT\r\nUID:standup\r\nSUMMARY:Standup\r\nDTSTART:20261102T090000Z\r\nDTEND:20261102T091500Z\r\n"
    "RRULE:FREQ=WEEKLY;COUNT=4\r\nEXDATE:20261109T090000Z\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:standup\r\nRECURRENCE-ID:20261116T090000Z\r\nSUMMARY:Standup (late)\r\n"
    "DTSTART:20261116T110000Z\r\nDTEND:20261116T111500Z\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:trip\r\nSUMMARY:Trip\r\nDTSTART;VALUE=DATE:20261105\r\nDTEND;VALUE=DATE:20261108\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:focus\r\nSUMMARY:Focus\r\nDTSTART:20261103T130000Z\r\nDTEND:20261103T150000Z\r\nTRANSP:TRANSPARENT\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:gone\r\nSUMMARY:Cancelled\r\nDTSTART:20261104T130000Z\r\nDTEND:20261104T140000Z\r\nSTATUS:CANCELLED\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:doc\r\nSUMMARY:Dermatologist\r\nCLASS:PRIVATE\r\nDTSTART:20261106T100000Z\r\nDTEND:20261106T110000Z\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:blank\r\nDTSTART:20261106T150000Z\r\nDTEND:20261106T160000Z\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:nyc\r\nSUMMARY:NYC call\r\nDTSTART;TZID=America/New_York:20261110T090000\r\nDTEND;TZID=America/New_York:20261110T100000\r\nEND:VEVENT\r\n"
    "END:VCALENDAR\r\n";

qint64 utc(int y, int m, int d, int h, int min = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, min), QTimeZone::UTC).toMSecsSinceEpoch();
}

const RemoteEvent *find(const QList<RemoteEvent> &events, const QString &id)
{
    for (const RemoteEvent &e : events) {
        if (e.externalId == id)
            return &e;
    }
    return nullptr;
}

} // namespace

class TestSyncClients : public QObject
{
    Q_OBJECT

private slots:
    void icsExpandsRepeatsAndExceptions()
    {
        FeedSettings feed;
        feed.id = "work";
        feed.color = "#ff0000";
        feed.blocking = BlockingMode::BlockAll;
        QString error;
        const QDateTime from(QDate(2026, 11, 1), QTime(0, 0), QTimeZone::UTC);
        const QList<RemoteEvent> events = parseIcs(kIcs, feed, from, from.addDays(30), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QStringList ids;
        for (const RemoteEvent &e : events)
            ids.append(e.externalId);
        // Standup: 2nd skipped, 3rd moved, 4th as normal; cancelled one dropped.
        const QString w1 = QString::number(utc(2026, 11, 2, 9)), w3 = QString::number(utc(2026, 11, 16, 9)),
                      w4 = QString::number(utc(2026, 11, 23, 9));
        QCOMPARE(ids, QStringList({"blank", "doc", "focus", "nyc", "standup@" + w1, "standup@" + w3, "standup@" + w4, "trip"}));

        const RemoteEvent *moved = find(events, "standup@" + w3);
        QCOMPARE(moved->event.title, QStringLiteral("Standup (late)"));
        QCOMPARE(moved->event.startAt, utc(2026, 11, 16, 11));
        QCOMPARE(moved->event.endAt, utc(2026, 11, 16, 11, 15));
        QCOMPARE(moved->event.color, QStringLiteral("#ff0000"));

        const RemoteEvent *trip = find(events, "trip");
        QVERIFY(trip->event.allDay);
        QCOMPARE(trip->event.startDate, QDate(2026, 11, 5));
        QCOMPARE(trip->event.endDate, QDate(2026, 11, 7)); // DTEND is exclusive in iCal
        QCOMPARE(trip->event.blocking.mode, BlockingMode::None); // all-day: holidays and the like don't take the day

        QCOMPARE(find(events, "focus")->event.blocking.mode, BlockingMode::None);
        QCOMPARE(find(events, "nyc")->event.blocking.mode, BlockingMode::BlockAll);
        QCOMPARE(find(events, "doc")->event.title, QStringLiteral("Dermatologist")); // private, but it's your feed
        QCOMPARE(find(events, "blank")->event.title, QStringLiteral("(busy)"));    // busy-only feeds have no summary
        // 9am New York in November (EST, -5) is 14:00 UTC.
        QCOMPARE(find(events, "nyc")->event.startAt, utc(2026, 11, 10, 14));
    }

    void icsWindowAndJunk()
    {
        FeedSettings feed;
        const QDateTime from(QDate(2026, 11, 20), QTime(0, 0), QTimeZone::UTC);
        const QList<RemoteEvent> events = parseIcs(kIcs, feed, from, from.addDays(7));
        QCOMPARE(events.size(), 1); // just the last standup
        QString error;
        QVERIFY(parseIcs("<html>login</html>", feed, from, from.addDays(7), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void feedSettingsRoundTrip()
    {
        FeedSettings f;
        f.id = "x";
        f.name = "Work";
        f.url = "webcal://example.com/cal.ics";
        f.blocking = BlockingMode::TagsOnly;
        f.allowedTags = {"calls"};
        const FeedSettings back = FeedSettings::fromJson(f.toJson());
        QCOMPARE(back.blocking, BlockingMode::TagsOnly);
        QCOMPARE(back.allowedTags, QStringList({"calls"}));
        QVERIFY(back.dayZone.isEmpty());
        f.dayZone = "America/New_York";
        QCOMPARE(FeedSettings::fromJson(f.toJson()).dayZone, QStringLiteral("America/New_York"));
        QCOMPARE(FeedFetcher::normalise(f.url).toString(), QStringLiteral("https://example.com/cal.ics"));
        QVERIFY(FeedFetcher::normalise("/home/me/cal.ics").isLocalFile());
    }

    void declaredZoneFromTheFile()
    {
        QCOMPARE(declaredZone("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-WR-TIMEZONE:America/New_York\r\nEND:VCALENDAR\r\n"),
                 QStringLiteral("America/New_York"));
        QCOMPARE(declaredZone("BEGIN:VCALENDAR\nX-WR-TIMEZONE;VALUE=TEXT:Europe/Paris\nEND:VCALENDAR\n"), QStringLiteral("Europe/Paris"));
        QVERIFY(declaredZone("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n").isEmpty());
    }

    // ---- Linear ----

    void linearIssuesMapAndPage()
    {
        FakeServer server;
        QNetworkAccessManager nam;
        LinearClient client(&nam);
        client.setEndpoint(server.url());
        client.setApiKey("lin_api_test");

        const QJsonObject issue{{"id", "uuid-1"}, {"identifier", "ENG-1"}, {"title", "Fix login"}, {"description", "steps"},
                                {"priority", 1}, {"dueDate", "2026-11-03"}, {"url", "https://linear.app/a/issue/ENG-1"},
                                {"state", QJsonObject{{"type", "started"}}}, {"team", QJsonObject{{"key", "ENG"}}},
                                {"labels", QJsonObject{{"nodes", QJsonArray{QJsonObject{{"name", "Bug"}}}}}}};
        QJsonObject second = issue;
        second["id"] = "uuid-2";
        second["priority"] = 0;
        server.reply(200, {{"data", QJsonObject{{"issues", QJsonObject{{"nodes", QJsonArray{issue}},
                                                                         {"pageInfo", QJsonObject{{"hasNextPage", true}, {"endCursor", "c1"}}}}}}}});
        server.reply(200, {{"data", QJsonObject{{"issues", QJsonObject{{"nodes", QJsonArray{second}},
                                                                         {"pageInfo", QJsonObject{{"hasNextPage", false}}}}}}}});

        LinearFilter filter;
        filter.teamKeys = {"ENG"};
        QList<RemoteTask> got;
        bool finished = false;
        client.activeIssues(filter, [&](const QList<RemoteTask> &issues, const QString &error, bool) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            got = issues;
            finished = true;
        });
        QTRY_VERIFY(finished);
        QCOMPARE(got.size(), 2);
        QCOMPARE(got[0].externalId, QStringLiteral("uuid-1"));
        QCOMPARE(got[0].priority, Priority::Urgent);
        QCOMPARE(got[0].dueDate, QDate(2026, 11, 3));
        QCOMPARE(got[0].tags, QStringList({"linear", "linear:ENG", "Bug"}));
        QCOMPARE(got[1].priority, Priority::Medium);

        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(server.requests[0].headers.value("authorization"), QStringLiteral("lin_api_test")); // no "Bearer"
        const QJsonObject vars = server.requests[0].body.value("variables").toObject();
        QCOMPARE(vars.value("filter").toObject().value("assignee").toObject().value("isMe").toObject().value("eq").toBool(), true);
        QCOMPARE(vars.value("filter").toObject().value("team").toObject().value("key").toObject().value("in").toArray().first().toString(),
                 QStringLiteral("ENG"));
        QCOMPARE(server.requests[1].body.value("variables").toObject().value("after").toString(), QStringLiteral("c1"));
    }

    void linearBadKey()
    {
        FakeServer server;
        QNetworkAccessManager nam;
        LinearClient client(&nam);
        client.setEndpoint(server.url());
        client.setApiKey("wrong");
        server.reply(400, {{"errors", QJsonArray{QJsonObject{{"message", "Authentication required"},
                                                             {"extensions", QJsonObject{{"code", "AUTHENTICATION_ERROR"}}}}}}});
        bool auth = false, finished = false;
        client.viewer([&](const QString &, const QVariantList &, const QString &error, bool authFailed) {
            QVERIFY(!error.isEmpty());
            auth = authFailed;
            finished = true;
        });
        QTRY_VERIFY(finished);
        QVERIFY(auth);
    }

    void linearWhatBecameOf()
    {
        FakeServer server;
        QNetworkAccessManager nam;
        LinearClient client(&nam);
        client.setEndpoint(server.url());
        client.setApiKey("k");
        const auto node = [](const char *id, const char *state, bool mine, QJsonValue archived = QJsonValue::Null) {
            return QJsonObject{{"id", id}, {"state", QJsonObject{{"type", state}}}, {"assignee", QJsonObject{{"isMe", mine}}},
                               {"team", QJsonObject{{"key", "ENG"}}}, {"archivedAt", archived}};
        };
        QJsonObject done = node("done", "completed", true);
        done.insert("startedAt", "2026-11-05T01:00:00.000Z");
        done.insert("completedAt", "2026-11-05T01:40:00.000Z");
        server.reply(200, {{"data", QJsonObject{{"issues", QJsonObject{{"nodes", QJsonArray{
                                                                done, node("nope", "canceled", true),
                                                                node("theirs", "started", false), node("later", "backlog", true),
                                                                node("archived", "started", true, "2026-10-01T00:00:00Z")}}}}}}});
        QHash<QString, Gone> gone;
        QHash<QString, Worked> worked;
        bool finished = false;
        client.whatBecameOf({"done", "nope", "theirs", "later", "archived", "deleted"}, LinearFilter(),
                            [&](const QHash<QString, Gone> &g, const QHash<QString, Worked> &w, const QString &error) {
                                QVERIFY(error.isEmpty());
                                gone = g;
                                worked = w;
                                finished = true;
                            });
        QTRY_VERIFY(finished);
        QCOMPARE(gone.value("done"), Gone::Completed);
        QCOMPARE(gone.value("nope"), Gone::Cancelled);
        QCOMPARE(gone.value("theirs"), Gone::Elsewhere);
        QCOMPARE(gone.value("later"), Gone::Elsewhere);
        QCOMPARE(gone.value("archived"), Gone::Cancelled);
        QCOMPARE(gone.value("deleted"), Gone::Cancelled);
        QCOMPARE(worked.size(), 1); // only the done one
        QCOMPARE(worked.value("done").startedAt, QDateTime(QDate(2026, 11, 5), QTime(1, 0), QTimeZone::UTC).toMSecsSinceEpoch());
        QCOMPARE(worked.value("done").completedAt, QDateTime(QDate(2026, 11, 5), QTime(1, 40), QTimeZone::UTC).toMSecsSinceEpoch());
    }

    void linearCompletePicksFirstDoneState()
    {
        FakeServer server;
        QNetworkAccessManager nam;
        LinearClient client(&nam);
        client.setEndpoint(server.url());
        client.setApiKey("k");
        const QJsonArray states{QJsonObject{{"id", "s-review"}, {"type", "started"}, {"position", 1}},
                                QJsonObject{{"id", "s-shipped"}, {"type", "completed"}, {"position", 5}},
                                QJsonObject{{"id", "s-done"}, {"type", "completed"}, {"position", 3}}};
        server.reply(200, {{"data", QJsonObject{{"issue", QJsonObject{{"state", QJsonObject{{"type", "started"}}},
                                                                        {"team", QJsonObject{{"states", QJsonObject{{"nodes", states}}}}}}}}}});
        server.reply(200, {{"data", QJsonObject{{"issueUpdate", QJsonObject{{"success", true}}}}}});
        QString result = "unset";
        client.complete("uuid-1", [&](const QString &error) { result = error; });
        QTRY_VERIFY(result != "unset");
        QVERIFY2(result.isEmpty(), qPrintable(result));
        QCOMPARE(server.requests.last().body.value("variables").toObject().value("stateId").toString(), QStringLiteral("s-done"));
    }
};

QTEST_GUILESS_MAIN(TestSyncClients)
#include "tst_syncclients.moc"
