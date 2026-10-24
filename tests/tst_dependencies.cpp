#include "dependencygraph.h"

#include <QTest>

using namespace tumbler;
using Add = DependencyGraph::AddResult;

class TestDependencies : public QObject
{
    Q_OBJECT

private slots:
    void rejectsSelfAndCycles()
    {
        DependencyGraph g;
        QCOMPARE(g.add("a", "a"), Add::SelfDependency);
        QCOMPARE(g.add("b", "a"), Add::Added);       // b waits on a
        QCOMPARE(g.add("c", "b"), Add::Added);       // c waits on b
        QCOMPARE(g.add("a", "c"), Add::WouldCreateCycle);
        QCOMPARE(g.add("a", "b"), Add::WouldCreateCycle);
        QCOMPARE(g.add("c", "b"), Add::AlreadyExists);
        QCOMPARE(g.add("c", "a"), Add::Added);       // redundant but not a cycle
        QCOMPARE(g.edgeCount(), 3);
    }

    void removeAndDependents()
    {
        DependencyGraph g;
        g.add("b", "a");
        g.add("c", "a");
        QCOMPARE(g.dependentsOf("a"), QSet<QString>({"b", "c"}));
        QVERIFY(g.remove("b", "a"));
        QVERIFY(!g.remove("b", "a"));
        QCOMPARE(g.dependentsOf("a"), QSet<QString>({"c"}));
        g.removeTask("a");
        QCOMPARE(g.edgeCount(), 0);
        QVERIFY(g.prerequisitesOf("c").isEmpty());
    }

    void blockersIgnoreFinishedPrerequisites()
    {
        DependencyGraph g;
        g.add("task", "done");
        g.add("task", "cancelled");
        g.add("task", "open");
        g.add("task", "started");
        const QHash<QString, TaskStatus> status{
            {"done", TaskStatus::Completed},
            {"cancelled", TaskStatus::Cancelled},
            {"open", TaskStatus::Pending},
            {"started", TaskStatus::InProgress},
        };
        const auto blockers = g.blockers("task", [&](const QString &id) { return status.value(id); });
        QCOMPARE(blockers, QList<QString>({"open", "started"}));
    }

    void topologicalOrderIsStable()
    {
        DependencyGraph g;
        g.add("write", "research");
        g.add("publish", "write");
        g.add("publish", "review");
        const auto order = g.topologicalOrder({"publish", "review", "write", "research", "unrelated"});
        QVERIFY(order);
        QCOMPARE(*order, QList<QString>({"review", "research", "write", "publish", "unrelated"}));
    }

    void parentAutoCompletion()
    {
        QVERIFY(!shouldAutoCompleteParent({}));
        QVERIFY(shouldAutoCompleteParent({TaskStatus::Completed, TaskStatus::Cancelled}));
        QVERIFY(!shouldAutoCompleteParent({TaskStatus::Completed, TaskStatus::InProgress}));
    }
};

QTEST_GUILESS_MAIN(TestDependencies)
#include "tst_dependencies.moc"
