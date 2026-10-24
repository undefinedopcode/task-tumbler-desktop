#pragma once

#include "types.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <functional>
#include <optional>

namespace tumbler {

// Prerequisite edges between tasks: "task depends on prerequisite".
// Pure in-memory structure; repositories load it from task_dependencies.
class DependencyGraph
{
public:
    enum class AddResult { Added, AlreadyExists, SelfDependency, WouldCreateCycle };

    AddResult add(const QString &task, const QString &prerequisite);
    bool remove(const QString &task, const QString &prerequisite);
    void removeTask(const QString &task);

    QSet<QString> prerequisitesOf(const QString &task) const;
    QSet<QString> dependentsOf(const QString &task) const;

    // True when adding task -> prerequisite would close a loop, i.e. the
    // prerequisite already (transitively) depends on the task.
    bool wouldCreateCycle(const QString &task, const QString &prerequisite) const;

    // Prerequisites that are not finished yet. A cancelled prerequisite no
    // longer blocks (the web app kept blocking on it forever).
    QList<QString> blockers(const QString &task, const std::function<TaskStatus(const QString &)> &statusOf) const;

    // Every task in the graph ordered so prerequisites come first; ties keep
    // the order of `tasks`. Returns nullopt if the graph has a cycle, which can
    // only happen with data imported from outside this class.
    std::optional<QList<QString>> topologicalOrder(const QList<QString> &tasks) const;

    int edgeCount() const;

private:
    QHash<QString, QSet<QString>> m_prerequisites; // task -> what it waits on
    QHash<QString, QSet<QString>> m_dependents;    // prerequisite -> who waits on it
};

// A parent task completes automatically once every subtask is finished.
// `subtaskStatuses` are the statuses of all of the parent's subtasks.
bool shouldAutoCompleteParent(const QList<TaskStatus> &subtaskStatuses);

} // namespace tumbler
