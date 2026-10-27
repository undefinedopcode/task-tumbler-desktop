#pragma once

#include "entities.h"
#include "queries.h"

#include <QAbstractListModel>

namespace tumbler {

// All tasks with what the list needs to show at a glance (blocked, subtask
// progress, next block), filtered and sorted for the Tasks page.
class TaskListModel : public QAbstractListModel
{
    Q_OBJECT

    // "active" (pending + in progress), "completed", "all"
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString categoryId READ categoryId WRITE setCategoryId NOTIFY categoryIdChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalActive READ totalActive NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        DescriptionRole,
        StatusRole,
        PriorityRole,
        EnergyRole,
        EstimateRole,
        DueDateRole,
        DueStateRole, // "", "overdue", "today", "soon"
        TagsRole,
        CategoryNameRole,
        CategoryColorRole,
        IsRecurringRole,
        RecurrenceTextRole,
        BlockedRole,
        SubtasksDoneRole,
        SubtasksTotalRole,
        ParentTitleRole,
        NextBlockRole, // ms of the next open block, 0 if none
        LoggedMinutesRole,
        ColorRole,
    };

    explicit TaskListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &f);
    QString search() const { return m_search; }
    void setSearch(const QString &s);
    QString categoryId() const { return m_categoryId; }
    void setCategoryId(const QString &id);
    int totalActive() const { return m_totalActive; }

    void reload(const Queries &q, qint64 now);

    // The filtered rows as maps keyed by role name, for the web UI (which
    // can't read a model); `rowsChanged` says when to ask again.
    Q_INVOKABLE QVariantList rows() const;

Q_SIGNALS:
    void rowsChanged();
    void filterChanged();
    void searchChanged();
    void categoryIdChanged();
    void countChanged();

private:
    struct Row {
        Task task;
        bool blocked = false;
        int subtasksDone = 0;
        int subtasksTotal = 0;
        QString parentTitle;
        QString categoryName;
        QString categoryColor;
        qint64 nextBlock = 0;
        int loggedMinutes = 0;
        QString dueState;
    };

    void rebuild();

    QList<Row> m_all;
    QList<Row> m_rows;
    QString m_filter = QStringLiteral("active");
    QString m_search;
    QString m_categoryId;
    int m_totalActive = 0;
};

} // namespace tumbler
