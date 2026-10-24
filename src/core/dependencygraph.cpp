#include "dependencygraph.h"

#include <QQueue>
#include <algorithm>

namespace tumbler {

DependencyGraph::AddResult DependencyGraph::add(const QString &task, const QString &prerequisite)
{
    if (task == prerequisite)
        return AddResult::SelfDependency;
    if (m_prerequisites.value(task).contains(prerequisite))
        return AddResult::AlreadyExists;
    if (wouldCreateCycle(task, prerequisite))
        return AddResult::WouldCreateCycle;

    m_prerequisites[task].insert(prerequisite);
    m_dependents[prerequisite].insert(task);
    return AddResult::Added;
}

bool DependencyGraph::remove(const QString &task, const QString &prerequisite)
{
    auto it = m_prerequisites.find(task);
    if (it == m_prerequisites.end() || !it->remove(prerequisite))
        return false;
    if (it->isEmpty())
        m_prerequisites.erase(it);

    auto dit = m_dependents.find(prerequisite);
    if (dit != m_dependents.end()) {
        dit->remove(task);
        if (dit->isEmpty())
            m_dependents.erase(dit);
    }
    return true;
}

void DependencyGraph::removeTask(const QString &task)
{
    for (const QString &p : m_prerequisites.value(task))
        remove(task, p);
    for (const QString &d : m_dependents.value(task))
        remove(d, task);
}

QSet<QString> DependencyGraph::prerequisitesOf(const QString &task) const
{
    return m_prerequisites.value(task);
}

QSet<QString> DependencyGraph::dependentsOf(const QString &task) const
{
    return m_dependents.value(task);
}

bool DependencyGraph::wouldCreateCycle(const QString &task, const QString &prerequisite) const
{
    if (task == prerequisite)
        return true;

    // Walk everything the prerequisite waits on; reaching `task` means the new
    // edge would close a loop.
    QQueue<QString> queue;
    QSet<QString> visited;
    queue.enqueue(prerequisite);
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        if (current == task)
            return true;
        if (visited.contains(current))
            continue;
        visited.insert(current);
        for (const QString &next : m_prerequisites.value(current))
            queue.enqueue(next);
    }
    return false;
}

QList<QString> DependencyGraph::blockers(const QString &task,
                                         const std::function<TaskStatus(const QString &)> &statusOf) const
{
    QList<QString> result;
    for (const QString &p : m_prerequisites.value(task)) {
        if (!isFinished(statusOf(p)))
            result.append(p);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::optional<QList<QString>> DependencyGraph::topologicalOrder(const QList<QString> &tasks) const
{
    // Kahn's algorithm over the given tasks, always taking the earliest-listed
    // ready task so the result is stable.
    const QSet<QString> members(tasks.begin(), tasks.end());
    QHash<QString, int> waiting;
    for (const QString &t : tasks) {
        int n = 0;
        for (const QString &p : m_prerequisites.value(t)) {
            if (members.contains(p))
                ++n;
        }
        waiting.insert(t, n);
    }

    QList<QString> order;
    QSet<QString> placed;
    order.reserve(tasks.size());
    while (order.size() < tasks.size()) {
        bool progressed = false;
        for (const QString &t : tasks) {
            if (placed.contains(t) || waiting.value(t) != 0)
                continue;
            order.append(t);
            placed.insert(t);
            for (const QString &d : m_dependents.value(t)) {
                if (waiting.contains(d))
                    --waiting[d];
            }
            progressed = true;
            break;
        }
        if (!progressed)
            return std::nullopt;
    }
    return order;
}

int DependencyGraph::edgeCount() const
{
    int n = 0;
    for (const auto &set : m_prerequisites)
        n += set.size();
    return n;
}

bool shouldAutoCompleteParent(const QList<TaskStatus> &subtaskStatuses)
{
    if (subtaskStatuses.isEmpty())
        return false;
    return std::all_of(subtaskStatuses.begin(), subtaskStatuses.end(), isFinished);
}

} // namespace tumbler
