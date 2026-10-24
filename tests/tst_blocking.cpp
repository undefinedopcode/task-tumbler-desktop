#include "blocking.h"

#include <QTest>

using namespace tumbler;

class TestBlocking : public QObject
{
    Q_OBJECT

private slots:
    void modes_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QStringList>("allowedTags");
        QTest::addColumn<bool>("recurring");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<bool>("allowed");

        QTest::newRow("none allows anything") << "none" << QStringList() << false << QStringList() << true;
        QTest::newRow("block_all blocks recurring") << "block_all" << QStringList() << true << QStringList() << false;
        QTest::newRow("recurring_only allows recurring") << "recurring_only" << QStringList() << true << QStringList() << true;
        QTest::newRow("recurring_only blocks one-off") << "recurring_only" << QStringList() << false << QStringList({"x"}) << false;
        QTest::newRow("tags_only match") << "tags_only" << QStringList({"errands", "phone"}) << false << QStringList({"phone"}) << true;
        QTest::newRow("tags_only case and space") << "tags_only" << QStringList({"Phone"}) << false << QStringList({" phone "}) << true;
        QTest::newRow("tags_only no match") << "tags_only" << QStringList({"phone"}) << true << QStringList({"work"}) << false;
        QTest::newRow("tags_only untagged") << "tags_only" << QStringList({"phone"}) << false << QStringList() << false;
    }

    void modes()
    {
        QFETCH(QString, mode);
        QFETCH(QStringList, allowedTags);
        QFETCH(bool, recurring);
        QFETCH(QStringList, tags);
        QFETCH(bool, allowed);

        const auto m = blockingModeFromString(mode);
        QVERIFY(m);
        QCOMPARE(nameOf(*m), mode);
        const EventBlockingRule rule{*m, allowedTags};
        QCOMPARE(rule.allows(recurring, tags), allowed);
    }

    void unknownMode()
    {
        QVERIFY(!blockingModeFromString("sometimes"));
    }
};

QTEST_GUILESS_MAIN(TestBlocking)
#include "tst_blocking.moc"
