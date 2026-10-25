#include "database.h"
#include "notes.h"
#include "operations.h"

#include <QSqlQuery>
#include <QTest>

using namespace tumbler;

namespace {

constexpr qint64 kNow = 1'770'000'000'000;

Note make(const QString &id, const QString &title, const QString &content, const QStringList &tags = {}, const QString &task = {})
{
    Note n;
    n.id = id;
    n.title = title;
    n.content = content;
    n.tags = tags;
    n.taskId = task;
    return n;
}

QStringList ids(const QList<NoteHit> &hits)
{
    QStringList out;
    for (const NoteHit &h : hits)
        out.append(h.note.id);
    return out;
}

} // namespace

class TestNotes : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> m_db;
    QSqlDatabase db() const { return m_db->handle(); }
    Queries q() const { return Queries(db()); }

    std::optional<AppliedMutation> run(const Mutation &m, qint64 at = kNow)
    {
        Q_UNUSED(at)
        QString error;
        auto applied = MutationRunner(db()).run(m, &error);
        if (!applied)
            qWarning() << error;
        return applied;
    }

private slots:
    void init()
    {
        m_db = std::make_unique<Database>();
        QVERIFY(m_db->open(":memory:") && m_db->migrate());
        Task t;
        t.id = "t1";
        t.title = "Quarterly report";
        QVERIFY(run(ops::createTask(t, kNow)));
        QVERIFY(run(notes::save(make("a", "Meeting with Sam", "Agreed the **budget** for Q3.\n\nNext: send slides.", {"work", "#Sam"}, "t1"), true, kNow)));
        QVERIFY(run(notes::save(make("b", "Shopping", "milk, eggs, coffee beans", {"home"}), true, kNow + 1000)));
        QVERIFY(run(notes::save(make("c", "Ideas", "A meetup for ADHD coders?", {"work"}), true, kNow + 2000)));
    }

    void ftsQueryIsSafe()
    {
        QCOMPARE(notes::ftsQuery("  meet sam "), QStringLiteral("\"meet\"* \"sam\"*"));
        QCOMPARE(notes::ftsQuery("\"quoted\" OR NEAR(x) -y"), QStringLiteral("\"quoted\"* \"OR\"* \"NEAR\"* \"x\"* \"y\"*"));
        QCOMPARE(notes::ftsQuery("café 2026"), QStringLiteral("\"café\"* \"2026\"*"));
        QVERIFY(notes::ftsQuery("  ** ").isEmpty());
    }

    void listsNewestFirst()
    {
        const QList<NoteHit> all = notes::find(q());
        QCOMPARE(ids(all), QStringList({"c", "b", "a"}));
        QCOMPARE(all.last().taskTitle, QStringLiteral("Quarterly report"));
        QCOMPARE(all.last().snippet, QStringLiteral("Agreed the budget for Q3. Next: send slides."));
        // Tags: deduplicated, '#' dropped, most used first.
        QCOMPARE(notes::get(q(), "a")->tags, QStringList({"work", "Sam"}));
        QCOMPARE(notes::tags(q()), QStringList({"work", "home", "Sam"}));
    }

    void searchesByPrefixInTitleAndBody()
    {
        QCOMPARE(ids(notes::find(q(), "meet")), QStringList({"a", "c"})); // "Meeting" (title) and "meetup"
        QCOMPARE(ids(notes::find(q(), "budg slides")), QStringList({"a"}));
        QCOMPARE(ids(notes::find(q(), "budget milk")), QStringList());
        const NoteHit hit = notes::find(q(), "coffee").first();
        QVERIFY(hit.snippet.contains(QStringLiteral("\x02" "coffee\x03")));
        // Junk that would be an FTS5 syntax error is just words.
        QCOMPARE(ids(notes::find(q(), "\"eggs")), QStringList({"b"}));
        QCOMPARE(ids(notes::find(q(), "NOT(")), QStringList());
    }

    void filtersByTagAndTask()
    {
        QCOMPARE(ids(notes::find(q(), {}, "WORK")), QStringList({"c", "a"}));
        QCOMPARE(ids(notes::find(q(), "meet", "work")), QStringList({"a", "c"}));
        QCOMPARE(ids(notes::find(q(), {}, {}, "t1")), QStringList({"a"}));
    }

    void editsAndUndoKeepTheIndexInStep()
    {
        Note a = *notes::get(q(), "a");
        a.content = "Moved to Thursday";
        QVERIFY(run(notes::save(a, false, kNow + 5000)));
        QCOMPARE(ids(notes::find(q(), "budget")), QStringList());
        QCOMPARE(ids(notes::find(q(), "thursday")), QStringList({"a"}));
        QCOMPARE(notes::find(q()).first().note.id, QStringLiteral("a")); // newest edit first

        MutationRunner runner(db());
        const auto deleted = runner.run(notes::remove("a"));
        QVERIFY(deleted);
        QCOMPARE(ids(notes::find(q(), "thursday")), QStringList());
        QVERIFY(runner.revert(*deleted));
        QCOMPARE(ids(notes::find(q(), "thursday")), QStringList({"a"}));
        // The FTS index agrees with the table (no stale rows).
        QSqlQuery check(db());
        QVERIFY(check.exec("INSERT INTO notes_fts(notes_fts, rank) VALUES ('integrity-check', 1)"));
    }

    void deletingTheTaskKeepsTheNote()
    {
        QVERIFY(run(ops::deleteTask("t1")));
        const auto a = notes::get(q(), "a");
        QVERIFY(a);
        QVERIFY(a->taskId.isEmpty());
    }

    void emptyTitleGetsOne()
    {
        Note bare;
        bare.id = "e";
        QVERIFY(run(notes::save(bare, true, kNow))); // nothing set at all
        QCOMPARE(notes::get(q(), "e")->content, QString());
        QVERIFY(run(notes::save(make("d", "  ", "text"), true, kNow)));
        QCOMPARE(notes::get(q(), "d")->title, QStringLiteral("Untitled note"));
    }
};

QTEST_GUILESS_MAIN(TestNotes)
#include "tst_notes.moc"
