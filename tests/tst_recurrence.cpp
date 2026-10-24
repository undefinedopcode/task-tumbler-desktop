#include "recurrence.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace tumbler;

namespace {

QDate d(const char *iso)
{
    return QDate::fromString(QLatin1String(iso), Qt::ISODate);
}

QStringList isoDates(const QList<QDate> &dates)
{
    QStringList out;
    for (const QDate &date : dates)
        out.append(date.toString(Qt::ISODate));
    return out;
}

RecurrencePattern parse(const char *json)
{
    QString error;
    auto p = RecurrencePattern::fromJson(QJsonDocument::fromJson(json).object(), &error);
    if (!p)
        qFatal("bad pattern %s: %s", json, qPrintable(error));
    return *p;
}

} // namespace

class TestRecurrence : public QObject
{
    Q_OBJECT

private slots:
    void dailyEveryDay()
    {
        const auto p = parse(R"({"type":"daily","interval":1,"startDate":"2026-03-04"})");
        QCOMPARE(isoDates(p.occurrencesBetween(d("2026-03-01"), d("2026-03-06"))),
                 QStringList({"2026-03-04", "2026-03-05", "2026-03-06"}));
    }

    void dailyIntervalAlignsToStart()
    {
        const auto p = parse(R"({"type":"daily","interval":3,"startDate":"2026-03-04"})");
        // 3/4, 3/7, 3/10, 3/13 ... asking from 3/8 must not restart the count.
        QCOMPARE(isoDates(p.occurrencesBetween(d("2026-03-08"), d("2026-03-14"))),
                 QStringList({"2026-03-10", "2026-03-13"}));
        QVERIFY(p.occursOn(d("2026-03-07")));
        QVERIFY(!p.occursOn(d("2026-03-08")));
    }

    void nothingBeforeStartOrAfterEnd()
    {
        const auto p = parse(R"({"type":"daily","interval":1,"startDate":"2026-03-04","endDate":"2026-03-05"})");
        QCOMPARE(isoDates(p.occurrencesBetween(d("2026-03-01"), d("2026-03-31"))),
                 QStringList({"2026-03-04", "2026-03-05"}));
        QVERIFY(!p.occursOn(d("2026-03-03")));
        QVERIFY(!p.occursOn(d("2026-03-06")));
    }

    void weeklyMidWeekStartKeepsFirstWeek()
    {
        // Created on a Wednesday for Mon/Wed/Fri: this Wednesday and Friday
        // count. The web app dropped them.
        const auto p = parse(R"({"type":"weekly","interval":1,"days":[1,3,5],"startDate":"2026-03-04"})");
        QCOMPARE(isoDates(p.occurrencesBetween(d("2026-03-01"), d("2026-03-10"))),
                 QStringList({"2026-03-04", "2026-03-06", "2026-03-09"}));
    }

    void fortnightlyCountsSundayWeeks()
    {
        // Start Wed 2026-03-04, so week 0 is Sun 3/1 - Sat 3/7.
        const auto p = parse(R"({"type":"weekly","interval":2,"days":[1,4],"startDate":"2026-03-04"})");
        QCOMPARE(isoDates(p.occurrencesBetween(d("2026-03-01"), d("2026-03-31"))),
                 QStringList({"2026-03-05", "2026-03-16", "2026-03-19", "2026-03-30"}));
    }

    void weekendDays()
    {
        const auto p = parse(R"({"type":"weekly","interval":1,"days":[0,6],"startDate":"2026-03-01"})");
        QCOMPARE(isoDates(p.occurrencesBetween(d("2026-03-01"), d("2026-03-08"))),
                 QStringList({"2026-03-01", "2026-03-07", "2026-03-08"}));
    }

    void leapDay()
    {
        const auto p = parse(R"({"type":"daily","interval":1,"startDate":"2028-02-28"})");
        QCOMPARE(isoDates(p.occurrencesBetween(d("2028-02-28"), d("2028-03-01"))),
                 QStringList({"2028-02-28", "2028-02-29", "2028-03-01"}));
    }

    void acceptsTimestampsInDateFields()
    {
        const auto p = parse(R"({"type":"daily","interval":1,"startDate":"2026-03-04T00:00:00.000Z"})");
        QCOMPARE(p.startDate, d("2026-03-04"));
    }

    void rejectsBadPatterns_data()
    {
        QTest::addColumn<QString>("json");
        QTest::newRow("unknown type") << R"({"type":"monthly","interval":1})";
        QTest::newRow("zero interval") << R"({"type":"daily","interval":0})";
        QTest::newRow("weekly no days") << R"({"type":"weekly","interval":1,"days":[]})";
        QTest::newRow("weekday out of range") << R"({"type":"weekly","interval":1,"days":[7]})";
        QTest::newRow("end before start") << R"({"type":"daily","interval":1,"startDate":"2026-03-04","endDate":"2026-03-01"})";
    }

    void rejectsBadPatterns()
    {
        QFETCH(QString, json);
        QVERIFY(!RecurrencePattern::fromJson(QJsonDocument::fromJson(json.toUtf8()).object()));
    }

    void jsonRoundTrip()
    {
        const auto p = parse(R"({"type":"weekly","interval":2,"days":[4,1,1],"startDate":"2026-03-04","endDate":"2026-06-01"})");
        QCOMPARE(p.days, QList<int>({1, 4}));
        QCOMPARE(*RecurrencePattern::fromJson(p.toJson()), p);
    }

    // Cases run through the web app's own generator. Where it agrees with the
    // intended semantics the port must match it; where it does not, the
    // fixture records why.
    void matchesLegacyGenerator()
    {
        QFile f(QStringLiteral(TUMBLER_FIXTURES "/legacy_recurrence.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonArray cases = QJsonDocument::fromJson(f.readAll()).array();
        QCOMPARE(cases.size(), 224);

        int agreed = 0;
        for (const QJsonValue &v : cases) {
            const QJsonObject c = v.toObject();
            const auto p = RecurrencePattern::fromJson(c.value(QLatin1String("pattern")).toObject());
            QVERIFY(p);

            QStringList expected, legacy;
            for (const QJsonValue &e : c.value(QLatin1String("expected")).toArray())
                expected.append(e.toString());
            for (const QJsonValue &e : c.value(QLatin1String("legacy")).toArray())
                legacy.append(e.toString());

            const QStringList actual = isoDates(p->occurrencesBetween(
                d(c.value(QLatin1String("from")).toString().toLatin1()), d(c.value(QLatin1String("to")).toString().toLatin1())));
            QCOMPARE(actual, expected);

            if (c.value(QLatin1String("divergence")).isNull()) {
                QCOMPARE(actual, legacy);
                ++agreed;
            } else {
                QVERIFY(actual != legacy);
            }
        }
        QCOMPARE(agreed, 154);
    }
};

QTEST_GUILESS_MAIN(TestRecurrence)
#include "tst_recurrence.moc"
