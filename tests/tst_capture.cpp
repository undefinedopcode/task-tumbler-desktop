#include "captureparser.h"

#include <QTest>

using namespace tumbler;

namespace {
const QDate kToday(2026, 10, 25); // a Sunday
}

class TestCapture : public QObject
{
    Q_OBJECT

private slots:
    void everything()
    {
        const auto r = parseCapture("call dentist tomorrow at 3pm ~15m #phone #health !high +low /errands", kToday);
        QCOMPARE(r.title, QStringLiteral("call dentist"));
        QCOMPARE(r.date, std::optional<QDate>(QDate(2026, 10, 26)));
        QCOMPARE(r.time, std::optional<QTime>(QTime(15, 0)));
        QCOMPARE(r.estimateMinutes, std::optional<int>(15));
        QCOMPARE(r.tags, QStringList({"phone", "health"}));
        QCOMPARE(r.priority, std::optional<Priority>(Priority::High));
        QCOMPARE(r.energy, std::optional<Energy>(Energy::Low));
        QCOMPARE(r.category, QStringLiteral("errands"));
        QVERIFY(!r.recurrence);
    }

    void plainTextIsUntouched()
    {
        const auto r = parseCapture("  buy sun cream and oat milk  ", kToday);
        QCOMPARE(r.title, QStringLiteral("buy sun cream and oat milk"));
        QVERIFY(!r.date && !r.time && !r.priority && r.tags.isEmpty());
    }

    void dates_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QDate>("date");
        QTest::newRow("today") << "x today" << kToday;
        QTest::newRow("tonight") << "x tonight" << kToday;
        QTest::newRow("in 3 days") << "x in 3 days" << QDate(2026, 10, 28);
        QTest::newRow("next week is monday") << "x next week" << QDate(2026, 10, 26);
        QTest::newRow("friday") << "x friday" << QDate(2026, 10, 30);
        QTest::newRow("on fri") << "x on fri" << QDate(2026, 10, 30);
        QTest::newRow("sunday on a sunday is next week") << "x sunday" << QDate(2026, 11, 1);
        QTest::newRow("iso") << "x 2026-12-01" << QDate(2026, 12, 1);
    }

    void dates()
    {
        QFETCH(QString, text);
        QFETCH(QDate, date);
        const auto r = parseCapture(text, kToday);
        QCOMPARE(r.date, std::optional<QDate>(date));
        QCOMPARE(r.title, QStringLiteral("x"));
    }

    void times_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QTime>("time");
        QTest::newRow("at 9") << "x at 9" << QTime(9, 0);
        QTest::newRow("@9:30am") << "x @9:30am" << QTime(9, 30);
        QTest::newRow("at 12am") << "x at 12am" << QTime(0, 0);
        QTest::newRow("at 12pm") << "x at 12pm" << QTime(12, 0);
        QTest::newRow("at 15:45") << "x at 15:45" << QTime(15, 45);
    }

    void times()
    {
        QFETCH(QString, text);
        QFETCH(QTime, time);
        QCOMPARE(parseCapture(text, kToday).time, std::optional<QTime>(time));
    }

    void bareNumbersStayInTitle()
    {
        QCOMPARE(parseCapture("read chapter 9", kToday).title, QStringLiteral("read chapter 9"));
        QVERIFY(!parseCapture("read chapter 9", kToday).time);
    }

    void estimates()
    {
        QCOMPARE(parseCapture("x ~1h30m", kToday).estimateMinutes, std::optional<int>(90));
        QCOMPARE(parseCapture("x ~2h", kToday).estimateMinutes, std::optional<int>(120));
        QCOMPARE(parseCapture("x ~45", kToday).estimateMinutes, std::optional<int>(45));
    }

    void priorities()
    {
        QCOMPARE(parseCapture("x !!!", kToday).priority, std::optional<Priority>(Priority::Urgent));
        QCOMPARE(parseCapture("x !!", kToday).priority, std::optional<Priority>(Priority::High));
        QCOMPARE(parseCapture("x !low", kToday).priority, std::optional<Priority>(Priority::Low));
        QCOMPARE(parseCapture("wow! great", kToday).title, QStringLiteral("wow! great"));
    }

    void recurrence_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("type"); // 0 daily, 1 weekly
        QTest::addColumn<int>("interval");
        QTest::addColumn<QList<int>>("days");
        QTest::newRow("daily") << "meds daily" << 0 << 1 << QList<int>();
        QTest::newRow("every 3 days") << "meds every 3 days" << 0 << 3 << QList<int>();
        QTest::newRow("weekdays") << "standup every weekday" << 1 << 1 << QList<int>({1, 2, 3, 4, 5});
        QTest::newRow("weekend") << "long run every weekend" << 1 << 1 << QList<int>({0, 6});
        QTest::newRow("mon and thu") << "gym every mon and thu" << 1 << 1 << QList<int>({1, 4});
        QTest::newRow("list") << "gym every monday, wednesday & friday" << 1 << 1 << QList<int>({1, 3, 5});
        QTest::newRow("every other") << "demo every other tuesday" << 1 << 2 << QList<int>({2});
        QTest::newRow("every 3 weeks") << "review every 3 weeks on fri" << 1 << 3 << QList<int>({5});
        QTest::newRow("weekly is today's weekday") << "bins weekly" << 1 << 1 << QList<int>({0});
    }

    void recurrence()
    {
        QFETCH(QString, text);
        QFETCH(int, type);
        QFETCH(int, interval);
        QFETCH(QList<int>, days);
        const auto r = parseCapture(text, kToday);
        QVERIFY(r.recurrence);
        QCOMPARE(int(r.recurrence->type), type);
        QCOMPARE(r.recurrence->interval, interval);
        QCOMPARE(r.recurrence->days, days);
        QCOMPARE(r.recurrence->startDate, kToday);
        QVERIFY(!r.date); // the day names went to the recurrence, not a due date
        QVERIFY(r.recurrence->isValid());
    }

    void recurringWithTime()
    {
        const auto r = parseCapture("take meds every day at 7:30pm", kToday);
        QCOMPARE(r.title, QStringLiteral("take meds"));
        QVERIFY(r.recurrence);
        QCOMPARE(r.time, std::optional<QTime>(QTime(19, 30)));
    }
};

QTEST_GUILESS_MAIN(TestCapture)
#include "tst_capture.moc"
