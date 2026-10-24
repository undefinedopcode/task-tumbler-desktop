#include "categoryschedule.h"
#include "types.h"

#include <QJsonDocument>
#include <QTest>

using namespace tumbler;

class TestCategorySchedule : public QObject
{
    Q_OBJECT

private slots:
    void weekdayConvention()
    {
        QCOMPARE(weekday0(QDate(2026, 10, 25)), 0); // Sunday
        QCOMPARE(weekday0(QDate(2026, 10, 26)), 1); // Monday
        QCOMPARE(weekday0(QDate(2026, 10, 31)), 6); // Saturday
    }

    // The web app computed new Date("2026-10-25").getDay(), which parses as
    // UTC midnight and reads Saturday in America/Vancouver. A Sunday must use
    // Sunday's window whatever the local zone is.
    void sundayIsSunday()
    {
        const auto s = CategorySchedule::fromJson(QJsonDocument::fromJson(R"({
            "0": {"enabled": true, "start": "10:00", "end": "12:00"},
            "6": {"enabled": true, "start": "14:00", "end": "16:00"}
        })").object());
        const auto w = s.windowFor(QDate(2026, 10, 25));
        QVERIFY(w);
        QCOMPARE(w->start, QTime(10, 0));
    }

    void disabledAndBrokenDaysAreOff()
    {
        const auto s = CategorySchedule::fromJson(QJsonDocument::fromJson(R"({
            "1": {"enabled": false, "start": "09:00", "end": "17:00"},
            "2": {"enabled": true},
            "3": {"enabled": true, "start": "17:00", "end": "09:00"},
            "4": {"enabled": true, "start": "09:00", "end": "17:00"}
        })").object());
        QVERIFY(!s.windowForWeekday(1));
        QVERIFY(!s.windowForWeekday(2));
        QVERIFY(!s.windowForWeekday(3));
        QCOMPARE(s.windowForWeekday(4), std::optional<TimeWindow>(TimeWindow{QTime(9, 0), QTime(17, 0)}));
        QVERIFY(!s.windowForWeekday(7));
    }

    void anytimeIsEveryDaySixToEleven()
    {
        const auto s = CategorySchedule::anytime();
        for (int day = 0; day < 7; ++day)
            QCOMPARE(s.windowForWeekday(day), std::optional<TimeWindow>(TimeWindow{QTime(6, 0), QTime(23, 0)}));
    }

    void weekdaysDefaultAndRoundTrip()
    {
        const auto s = CategorySchedule::weekdays(QTime(13, 0), QTime(21, 0));
        QVERIFY(!s.windowForWeekday(0));
        QVERIFY(!s.windowForWeekday(6));
        QCOMPARE(s.windowForWeekday(3)->end, QTime(21, 0));

        const auto again = CategorySchedule::fromJson(s.toJson());
        for (int day = 0; day < 7; ++day)
            QCOMPARE(again.windowForWeekday(day), s.windowForWeekday(day));
    }
};

QTEST_GUILESS_MAIN(TestCategorySchedule)
#include "tst_categoryschedule.moc"
