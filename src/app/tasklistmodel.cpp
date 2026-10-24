#include "tasklistmodel.h"

#include "dependencygraph.h"
#include "variants.h"

#include <QDateTime>

namespace tumbler {

namespace {
int priorityRank(Priority p)
{
    switch (p) {
    case Priority::Urgent:
        return 0;
    case Priority::High:
        return 1;
    case Priority::Medium:
        return 2;
    case Priority::Low:
        return 3;
    }
    return 2;
}
} // namespace

TaskListModel::TaskListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TaskListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant TaskListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    const Task &t = r.task;
    switch (role) {
    case IdRole:
        return t.id;
    case TitleRole:
        return t.title;
    case DescriptionRole:
        return t.description;
    case StatusRole:
        return nameOf(t.status);
    case PriorityRole:
        return nameOf(t.priority);
    case EnergyRole:
        return nameOf(t.energy);
    case EstimateRole:
        return t.estimateMinutes;
    case DueDateRole:
        return t.dueDate.isValid() ? t.dueDate.toString(Qt::ISODate) : QString();
    case DueStateRole:
        return r.dueState;
    case TagsRole:
        return t.tags;
    case CategoryNameRole:
        return r.categoryName;
    case CategoryColorRole:
        return r.categoryColor;
    case IsRecurringRole:
        return t.isRecurring();
    case RecurrenceTextRole:
        return t.recurrence ? describeRecurrence(*t.recurrence) : QString();
    case BlockedRole:
        return r.blocked;
    case SubtasksDoneRole:
        return r.subtasksDone;
    case SubtasksTotalRole:
        return r.subtasksTotal;
    case ParentTitleRole:
        return r.parentTitle;
    case NextBlockRole:
        return r.nextBlock;
    case LoggedMinutesRole:
        return r.loggedMinutes;
    case ColorRole:
        return t.color;
    }
    return {};
}

QHash<int, QByteArray> TaskListModel::roleNames() const
{
    return {
        {IdRole, "taskId"},
        {TitleRole, "title"},
        {DescriptionRole, "description"},
        {StatusRole, "status"},
        {PriorityRole, "priority"},
        {EnergyRole, "energy"},
        {EstimateRole, "estimate"},
        {DueDateRole, "dueDate"},
        {DueStateRole, "dueState"},
        {TagsRole, "tags"},
        {CategoryNameRole, "categoryName"},
        {CategoryColorRole, "categoryColor"},
        {IsRecurringRole, "isRecurring"},
        {RecurrenceTextRole, "recurrenceText"},
        {BlockedRole, "blocked"},
        {SubtasksDoneRole, "subtasksDone"},
        {SubtasksTotalRole, "subtasksTotal"},
        {ParentTitleRole, "parentTitle"},
        {NextBlockRole, "nextBlock"},
        {LoggedMinutesRole, "loggedMinutes"},
        {ColorRole, "taskColor"},
    };
}

void TaskListModel::setFilter(const QString &f)
{
    if (f == m_filter)
        return;
    m_filter = f;
    Q_EMIT filterChanged();
    rebuild();
}

void TaskListModel::setSearch(const QString &s)
{
    if (s == m_search)
        return;
    m_search = s;
    Q_EMIT searchChanged();
    rebuild();
}

void TaskListModel::setCategoryId(const QString &id)
{
    if (id == m_categoryId)
        return;
    m_categoryId = id;
    Q_EMIT categoryIdChanged();
    rebuild();
}

void TaskListModel::reload(const Queries &q, qint64 now)
{
    const QList<Task> tasks = q.tasks();
    QHash<QString, Task> byId;
    for (const Task &t : tasks)
        byId.insert(t.id, t);

    QHash<QString, QPair<QString, QString>> categories;
    for (const Category &c : q.categories())
        categories.insert(c.id, {c.name, c.color});

    DependencyGraph graph;
    for (const Dependency &d : q.dependencies())
        graph.add(d.taskId, d.dependsOnId);
    const auto statusOf = [&byId](const QString &id) { return byId.value(id).status; };

    QHash<QString, QPair<int, int>> subtasks;
    for (const Task &t : tasks) {
        if (t.parentId.isEmpty())
            continue;
        auto &c = subtasks[t.parentId];
        ++c.second;
        if (isFinished(t.status))
            ++c.first;
    }

    QHash<QString, qint64> nextBlock;
    for (const ScheduleBlock &s : q.schedules(now, std::numeric_limits<qint64>::max())) {
        if (!s.completed && !nextBlock.contains(s.taskId))
            nextBlock.insert(s.taskId, s.start);
    }
    const QHash<QString, int> logged = q.loggedMinutes();
    const QDate today = QDateTime::fromMSecsSinceEpoch(now).date();

    m_all.clear();
    m_totalActive = 0;
    for (const Task &t : tasks) {
        Row r;
        r.task = t;
        r.blocked = !isFinished(t.status) && !graph.blockers(t.id, statusOf).isEmpty();
        const auto c = subtasks.value(t.id);
        r.subtasksDone = c.first;
        r.subtasksTotal = c.second;
        r.parentTitle = byId.value(t.parentId).title;
        r.categoryName = categories.value(t.categoryId).first;
        r.categoryColor = categories.value(t.categoryId).second;
        r.nextBlock = nextBlock.value(t.id);
        r.loggedMinutes = logged.value(t.id);
        if (t.dueDate.isValid() && !isFinished(t.status)) {
            if (t.dueDate < today)
                r.dueState = QStringLiteral("overdue");
            else if (t.dueDate == today)
                r.dueState = QStringLiteral("today");
            else if (t.dueDate <= today.addDays(2))
                r.dueState = QStringLiteral("soon");
        }
        if (!isFinished(t.status))
            ++m_totalActive;
        m_all.append(r);
    }
    rebuild();
}

void TaskListModel::rebuild()
{
    const QString needle = m_search.trimmed();
    QList<Row> rows;
    for (const Row &r : std::as_const(m_all)) {
        const bool finished = isFinished(r.task.status);
        if (m_filter == QLatin1String("active") && finished)
            continue;
        if (m_filter == QLatin1String("completed") && !finished)
            continue;
        if (!m_categoryId.isEmpty() && r.task.categoryId != m_categoryId)
            continue;
        if (!needle.isEmpty()) {
            const bool hit = r.task.title.contains(needle, Qt::CaseInsensitive)
                || r.task.description.contains(needle, Qt::CaseInsensitive)
                || r.task.tags.contains(needle.startsWith(QLatin1Char('#')) ? needle.mid(1) : needle, Qt::CaseInsensitive);
            if (!hit)
                continue;
        } else if (!r.task.parentId.isEmpty() && m_filter != QLatin1String("completed")) {
            // Subtasks live on their parent's page unless searching.
            continue;
        }
        rows.append(r);
    }

    std::sort(rows.begin(), rows.end(), [this](const Row &a, const Row &b) {
        if (m_filter == QLatin1String("completed"))
            return a.task.updatedAt > b.task.updatedAt;
        // One-off tasks before routines; startable before blocked; in
        // progress first; then priority, due date.
        if (a.task.isRecurring() != b.task.isRecurring())
            return !a.task.isRecurring();
        if (a.blocked != b.blocked)
            return !a.blocked;
        const bool aStarted = a.task.status == TaskStatus::InProgress;
        const bool bStarted = b.task.status == TaskStatus::InProgress;
        if (aStarted != bStarted)
            return aStarted;
        if (a.task.priority != b.task.priority)
            return priorityRank(a.task.priority) < priorityRank(b.task.priority);
        if (a.task.dueDate.isValid() != b.task.dueDate.isValid())
            return a.task.dueDate.isValid();
        if (a.task.dueDate != b.task.dueDate)
            return a.task.dueDate < b.task.dueDate;
        if (a.task.sortOrder != b.task.sortOrder)
            return a.task.sortOrder.value_or(INT_MAX) < b.task.sortOrder.value_or(INT_MAX);
        return a.task.createdAt < b.task.createdAt;
    });

    beginResetModel();
    m_rows = rows;
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace tumbler
