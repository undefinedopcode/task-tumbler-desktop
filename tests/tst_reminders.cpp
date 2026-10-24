#include "reminderplan.h"

#include <QTest>

using namespace tumbler;

namespace {
constexpr qint64 kMin = 60 * 1000;
constexpr qint64 kNow = 1'770'000'000'000;

AgendaItem block(const QString &id, qint64 startOffsetMin, int minutes, AgendaItem::Kind kind = AgendaItem::Kind::Schedule)
{
    AgendaItem i;
    i.kind = kind;
    i.id = id;
    i.title = id;
    i.start = kNow + startOffsetMin * kMin;
    i.end = i.start + minutes * kMin;
    return i;
}

QStringList kinds(const QList<Reminder> &rs)
{
    static const char *const names[] = {"soon", "now", "wrap", "missed"};
    QStringList out;
    for (const Reminder &r : rs)
        out.append(QStringLiteral("%1:%2").arg(QLatin1String(names[int(r.kind)]), r.item.id));
    return out;
}
} // namespace

class TestReminders : public QObject
{
    Q_OBJECT

private slots:
    void startingSoonFiresOnceInsideTheLeadWindow()
    {
        TodayView v;
        v.upcoming = {block("in5", 5, 30), block("in4", 4, 30), block("in10", 10, 30), block("in2", 2, 30)};
        QSet<QString> fired;
        // 5-minute lead: 5 and 4 minutes out are inside [lead - grace, lead];
        // 2 minutes out is too late for a heads-up (it'll get "starting now").
        QCOMPARE(kinds(dueReminders(v, kNow, 5 * kMin, 0, fired)), QStringList({"soon:in5", "soon:in4"}));
        QVERIFY(dueReminders(v, kNow, 5 * kMin, 0, fired).isEmpty());
        // No heads-up at all with a zero lead.
        QSet<QString> none;
        QVERIFY(dueReminders(v, kNow, 0, 0, none).isEmpty());
    }

    void startingNowWithinGraceUnlessWorking()
    {
        TodayView v;
        AgendaItem started = block("started", -1, 30);
        AgendaItem late = block("late", -3, 30);
        AgendaItem working = block("working", 0, 30);
        working.working = true;
        v.now = {started, late, working};
        QSet<QString> fired;
        QCOMPARE(kinds(dueReminders(v, kNow, 5 * kMin, 0, fired)), QStringList({"now:started"}));
    }

    void timelessAndAllDayNeverRemind()
    {
        TodayView v;
        AgendaItem anytime = block("anytime", 0, 10, AgendaItem::Kind::Occurrence);
        anytime.timeless = true;
        AgendaItem allDay = block("holiday", 0, 1440, AgendaItem::Kind::Event);
        allDay.allDay = true;
        v.now = {anytime, allDay};
        QSet<QString> fired;
        QVERIFY(dueReminders(v, kNow, 5 * kMin, 0, fired).isEmpty());
    }

    void wrapUpForTheBlockBeingWorkedOn()
    {
        TodayView v;
        AgendaItem working = block("deep", -55, 59); // 4 minutes left
        working.working = true;
        AgendaItem other = block("other", -55, 59);
        v.now = {working, other};
        QSet<QString> fired;
        QCOMPARE(kinds(dueReminders(v, kNow, 5 * kMin, 0, fired)), QStringList({"wrap:deep"}));
    }

    void missedOnlyJustEndedAndAfterLaunch()
    {
        TodayView v;
        // Ended 1 min ago, ended 1.5 h ago, ended 1 min ago, and a recurring
        // item (only blocks get "missed").
        v.overdue = {block("just", -31, 30), block("old", -120, 30), block("alsoJust", -32, 31),
                     block("meds", -31, 30, AgendaItem::Kind::Occurrence)};
        QSet<QString> fired;
        const qint64 launchedAt = kNow - 90 * 1000; // both recent blocks ended after launch
        QCOMPARE(kinds(dueReminders(v, kNow, 5 * kMin, launchedAt, fired)), QStringList({"missed:just", "missed:alsoJust"}));

        // Launched just now: nothing that ended earlier is announced.
        QSet<QString> fresh;
        QVERIFY(dueReminders(v, kNow, 5 * kMin, kNow, fresh).isEmpty());
    }

    void bumpedBlockRemindsAgain()
    {
        TodayView v;
        v.upcoming = {block("b", 5, 30)};
        QSet<QString> fired;
        QCOMPARE(dueReminders(v, kNow, 5 * kMin, 0, fired).size(), 1);
        // Same block moved 15 minutes later is a new moment.
        v.upcoming = {block("b", 5 + 15, 30)};
        QCOMPARE(dueReminders(v, kNow + 15 * kMin, 5 * kMin, 0, fired).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestReminders)
#include "tst_reminders.moc"
