#pragma once

#include "database.h"
#include "mutation.h"
#include "queries.h"
#include "tasklistmodel.h"

#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>
#include <QUndoStack>
#include <QVariantMap>
#include <memory>
#include <optional>

namespace tumbler {



// The one object QML talks to. Every write goes through apply(): a Mutation
// from core/operations runs, lands on the undo stack, and `changed` tells
// views which tables moved. Reads are plain Q_INVOKABLEs returning maps.
class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantMap today READ today NOTIFY todayChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY categoriesChanged)
    Q_PROPERTY(tumbler::TaskListModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(QVariantMap working READ working NOTIFY workingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)
    Q_PROPERTY(bool needsOnboarding READ needsOnboarding NOTIFY onboardingChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString dataPath READ dataPath CONSTANT)

public:
    explicit AppController(Database &db, QObject *parent = nullptr);
    ~AppController() override;

    static AppController *create(QQmlEngine *, QJSEngine *);
    static void setInstance(AppController *instance);

    QVariantMap today() const { return m_today; }
    QVariantList categories() const { return m_categories; }
    TaskListModel *tasks() const { return m_tasks; }
    QVariantMap working() const { return m_working; }
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    QString undoText() const { return m_undo.undoText(); }
    QString redoText() const { return m_undo.redoText(); }
    bool needsOnboarding() const;
    QVariantMap settings() const { return m_settings; }
    QString dataPath() const { return m_db.path(); }

    QTimeZone zone() const;
    Queries queries() const { return Queries(m_db.handle()); }

    // --- capture --------------------------------------------------------
    Q_INVOKABLE QVariantMap previewCapture(const QString &text) const;
    // Creates the task (and a block when a time was given); returns its id.
    Q_INVOKABLE QString capture(const QString &text);

    // --- tasks ----------------------------------------------------------
    Q_INVOKABLE QString createTask(const QVariantMap &fields);
    Q_INVOKABLE bool updateTask(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool setTaskStatus(const QString &id, const QString &status);
    Q_INVOKABLE bool deleteTask(const QString &id);
    Q_INVOKABLE QString addSubtask(const QString &parentId, const QString &title);
    Q_INVOKABLE bool addDependency(const QString &taskId, const QString &prerequisiteId);
    Q_INVOKABLE bool removeDependency(const QString &taskId, const QString &prerequisiteId);
    // Everything the detail page shows: the task, subtasks, prerequisites,
    // dependents, blocks, work logged.
    Q_INVOKABLE QVariantMap taskDetail(const QString &id) const;
    // Tasks that could become a prerequisite of `taskId` (no loops).
    Q_INVOKABLE QVariantList prerequisiteCandidates(const QString &taskId) const;

    // --- scheduling -----------------------------------------------------
    Q_INVOKABLE bool scheduleNextSlot(const QString &taskId);
    Q_INVOKABLE bool scheduleAt(const QString &taskId, qint64 start, int minutes);
    Q_INVOKABLE bool moveSchedule(const QString &id, qint64 start);
    Q_INVOKABLE bool bump(const QString &scheduleId, int minutes);
    Q_INVOKABLE bool bumpToNextSlot(const QString &scheduleId);
    Q_INVOKABLE bool bumpToTomorrow(const QString &scheduleId);
    Q_INVOKABLE bool completeSchedule(const QString &id);
    Q_INVOKABLE bool unschedule(const QString &id);
    Q_INVOKABLE bool setOccurrence(const QString &id, const QString &status);
    // Items, category shading and all-day events for one local day.
    Q_INVOKABLE QVariantMap dayAgenda(const QString &isoDate) const;

    // --- work timer -----------------------------------------------------
    Q_INVOKABLE bool startWork(const QString &taskId);
    Q_INVOKABLE bool stopWork();

    // --- categories & settings -------------------------------------------
    Q_INVOKABLE QString saveCategory(const QVariantMap &category);
    Q_INVOKABLE bool deleteCategory(const QString &id);
    Q_INVOKABLE bool setSetting(const QString &key, const QVariant &value);

    // --- data -----------------------------------------------------------
    Q_INVOKABLE QString importExport(const QUrl &file);
    Q_INVOKABLE void startFresh();
    Q_INVOKABLE QString backupNow();
    Q_INVOKABLE void openDataFolder() const;

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Runs a mutation and puts it on the undo stack. Public for the desktop
    // services (notification actions) which share this path.
    bool apply(const std::optional<Mutation> &mutation, const QString &error = {});
    // Moves the database between states for the undo stack.
    bool revertApplied(const AppliedMutation &applied);
    bool reapplyApplied(const AppliedMutation &applied);

    // Daily backup kept in <data>/backups, last 14 days.
    void dailyBackup();

    qint64 now() const;

Q_SIGNALS:
    void changed(const QStringList &tables);
    void todayChanged();
    void categoriesChanged();
    void workingChanged();
    void undoStateChanged();
    void onboardingChanged();
    void settingsChanged();
    // Short message for a passive notification in the window.
    void message(const QString &text);
    void showCaptureRequested();
    void showWindowRequested();

private:
    void refresh(const QSet<QString> &tables);
    void refreshToday();
    void ensureOccurrences();
    QString resolveCategory(const QString &name) const;
    // The default category's id, creating "Default" (every day, 06:00-23:00)
    // if there is none yet. Not undoable: it's setup.
    QString ensureDefaultCategory();
    // After a move: a gentle note if the block now ends after the task's due date.
    void noteMove(const QString &scheduleId);

    Database &m_db;
    QUndoStack m_undo;
    TaskListModel *m_tasks;
    QVariantMap m_today;
    QVariantList m_categories;
    QVariantMap m_working;
    QVariantMap m_settings;
    QTimer m_tick;
    QDate m_occurrencesThrough;
    QDate m_lastTodayDate;
};

} // namespace tumbler
