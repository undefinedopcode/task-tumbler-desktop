#include "timeparse.h"

#include <QTest>

using namespace tumbler;

namespace {
const QTimeZone kVancouver("America/Vancouver");
const QTimeZone kUtc = QTimeZone::UTC;

QString utcIso(const QString &text, const QTimeZone &naive)
{
    const auto p = parseTimestamp(text, naive);
    return p ? p->utc.toString(Qt::ISODateWithMs) : QStringLiteral("invalid");
}
} // namespace

class TestTimeParse : public QObject
{
    Q_OBJECT

private slots:
    void formats_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("naiveIsLocal");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("hadOffset");

        QTest::newRow("js toISOString") << "2025-11-22T09:47:30.188Z" << true << "2025-11-22T09:47:30.188Z" << true;
        QTest::newRow("naive local") << "2025-12-03T12:15:00" << true << "2025-12-03T20:15:00.000Z" << false;
        QTest::newRow("sqlite datetime") << "2025-12-03 18:41:30" << false << "2025-12-03T18:41:30.000Z" << false;
        QTest::newRow("pg timestamp micros") << "2026-10-24 19:56:20.524079" << false << "2026-10-24T19:56:20.524Z" << false;
        QTest::newRow("pg timestamptz +00") << "2026-03-01 10:00:00+00" << true << "2026-03-01T10:00:00.000Z" << true;
        QTest::newRow("offset with colon") << "2026-03-01T10:00:00+05:30" << true << "2026-03-01T04:30:00.000Z" << true;
        QTest::newRow("negative offset") << "2026-03-01T10:00:00-0800" << false << "2026-03-01T18:00:00.000Z" << true;
        QTest::newRow("minutes only") << "2026-03-01T10:00" << false << "2026-03-01T10:00:00.000Z" << false;
        QTest::newRow("date only") << "2026-03-01" << false << "2026-03-01T00:00:00.000Z" << false;
        // Summer time: Vancouver is UTC-7 in July.
        QTest::newRow("naive local in DST") << "2026-07-01T09:00:00" << true << "2026-07-01T16:00:00.000Z" << false;
    }

    void formats()
    {
        QFETCH(QString, text);
        QFETCH(bool, naiveIsLocal);
        QFETCH(QString, expected);
        QFETCH(bool, hadOffset);

        const QTimeZone zone = naiveIsLocal ? kVancouver : kUtc;
        QCOMPARE(utcIso(text, zone), expected);
        QCOMPARE(parseTimestamp(text, zone)->hadOffset, hadOffset);
    }

    void rejectsGarbage()
    {
        QVERIFY(!parseTimestamp(QString(), kUtc));
        QVERIFY(!parseTimestamp("yesterday", kUtc));
        QVERIFY(!parseTimestamp("2026-13-01", kUtc));
        QVERIFY(!parseTimestamp("2026-02-01T25:00:00", kUtc));
        QVERIFY(!parseTimestampMs("2026-02-30", kUtc));
    }

    void localDates()
    {
        QCOMPARE(parseLocalDate("2025-11-19", kVancouver), QDate(2025, 11, 19));
        // Local midnight stored as a UTC instant belongs to the local day.
        QCOMPARE(parseLocalDate("2025-11-20T07:00:00.000Z", kVancouver), QDate(2025, 11, 19));
        // A naive date-time keeps its own date.
        QCOMPARE(parseLocalDate("2025-11-19T23:30:00", kVancouver), QDate(2025, 11, 19));
        QVERIFY(!parseLocalDate("", kVancouver).isValid());
    }
};

QTEST_GUILESS_MAIN(TestTimeParse)
#include "tst_timeparse.moc"
