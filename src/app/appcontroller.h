#pragma once

#include "captureparser.h"
#include "database.h"
#include "dayplanner.h"
#include "mutation.h"
#include "queries.h"
#include "tasklistmodel.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>
#include <QUndoStack>
#include <QVariantMap>
#include <memory>
#include <optional>

namespace tumbler {

namespace ai {
class Provider;
struct Result;
}
class KeyStore;

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
    // {provider: "off"|"anthropic"|"ollama", ready, busy, label, hasKey, model, ollamaUrl, ollamaModel, shareNotes}
    Q_PROPERTY(QVariantMap ai READ ai NOTIFY aiChanged)
    // Latest check-in today: {level, at, label}; empty if none. `due` is set
    // when it's been a while and a gentle prompt is appropriate.
    Q_PROPERTY(QVariantMap energy READ energy NOTIFY energyChanged)
    Q_PROPERTY(QStringList ollamaModels READ ollamaModels NOTIFY ollamaModelsChanged)
    // Set after coming back to the computer mid-session: {since, until,
    // minutes, title}. Cleared by resolveAway().
    Q_PROPERTY(QVariantMap away READ away NOTIFY awayChanged)
    Q_PROPERTY(bool focusMode READ focusMode WRITE setFocusMode NOTIFY focusModeChanged)

public:
    // `keys` is the keyring; without one (tests) the API key lives in memory.
    explicit AppController(Database &db, QObject *parent = nullptr, KeyStore *keys = nullptr);
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
    QVariantMap ai() const;
    QVariantMap energy() const { return m_energy; }
    QStringList ollamaModels() const { return m_ollamaModels; }
    QVariantMap away() const { return m_away; }
    bool focusMode() const { return m_focusMode; }
    void setFocusMode(bool on);

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

    // --- planning & AI (appcontroller_ai.cpp) --------------------------------
    // Builds a plan for the day; answers with planReady. With useAi and a
    // provider set up, the AI ranks and the offline planner times; if the AI
    // fails the offline plan comes back with the reason.
    Q_INVOKABLE void planDay(const QString &isoDate, bool useAi);
    // Schedules the chosen blocks as one undoable step.
    Q_INVOKABLE bool acceptPlan(const QVariantList &blocks, const QVariantMap &plan);
    Q_INVOKABLE void dismissPlan(const QVariantMap &plan);
    Q_INVOKABLE void breakDown(const QString &taskId);
    Q_INVOKABLE bool addSteps(const QString &taskId, const QVariantList &steps);
    // AI reading of a capture line; answers with aiCaptureReady. A following
    // capture() of the same text uses it.
    Q_INVOKABLE void aiCapture(const QString &text);

    Q_INVOKABLE bool logEnergy(int level, const QString &notes = {});
    Q_INVOKABLE bool saveHealth(const QVariantMap &log);
    Q_INVOKABLE QVariantMap healthToday() const;
    // {energy: [{at, level}], days: [{date, label, energy, sleep, readiness, hours}]}
    Q_INVOKABLE QVariantMap energyHistory(int days) const;

    Q_INVOKABLE QVariantMap eventDetail(const QString &id) const;
    Q_INVOKABLE QString saveEvent(const QVariantMap &event);
    Q_INVOKABLE bool deleteEvent(const QString &id);

    // Notes (appcontroller_notes.cpp). Lists: [{id, title, snippet (rich
    // text, matches in bold), tags, color, taskId, taskTitle, updatedAt}].
    Q_INVOKABLE QVariantList notes(const QString &search = {}, const QString &tag = {}, const QString &taskId = {}) const;
    Q_INVOKABLE QStringList noteTags() const;
    Q_INVOKABLE QVariantMap noteDetail(const QString &id) const;
    Q_INVOKABLE QString createNote(const QVariantMap &fields = {});
    // Saves as you type: not undoable step by step (the text box has its own undo).
    Q_INVOKABLE bool saveNote(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool deleteNote(const QString &id);
    Q_INVOKABLE bool exportNote(const QString &id, const QUrl &file);
    // Adds a paragraph to the end, as one undoable step (for other apps).
    Q_INVOKABLE bool appendToNote(const QString &id, const QString &text);
    // Open tasks a note can be about: [{id, title}] by title.
    Q_INVOKABLE QVariantList noteTaskChoices() const;

    // Looking back over the last `days` days including today, or everything
    // when 0 (appcontroller_stats.cpp). {from, to, workedMinutes, sessions,
    // averageSessionMinutes, longestSessionMinutes, finishedTasks,
    // finishedRoutines, bars: [{label, workedMinutes, finished}] (a bar a day,
    // or a week over two months, then weekly is true), hours: [{hour, minutes, energy}],
    // tasks: [{id, title, estimate, worked, sessions, finished}],
    // estimates: {tasks, ratio, estimated, worked, bySize: [{size, tasks,
    // ratio}], byCategory: [{name, tasks, ratio}]}, moving: [{id, title, bumps}],
    // planWithPace (the setting), paceActive (it has enough to go on)}
    Q_INVOKABLE QVariantMap stats(int days) const;

    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void testAi();
    Q_INVOKABLE void refreshOllamaModels();

    // --- launchers, focus, away (appcontroller_desktop.cpp) -------------------
    // kind: url | file | app | command. A command's target is the words of
    // the command line; it is split into argv here, never run by a shell.
    Q_INVOKABLE QString addAction(const QString &taskId, const QString &kind, const QString &target, bool onStart);
    Q_INVOKABLE bool removeAction(const QString &id);
    Q_INVOKABLE void runAction(const QString &id);
    // Remembers this exact command as allowed, then runs it.
    Q_INVOKABLE void approveCommand(const QString &actionId);
    Q_INVOKABLE QVariantList installedApps() const;
    // Starts work if needed and enters focus mode.
    Q_INVOKABLE void startFocus(const QString &taskId);
    // Called by the idle watcher.
    void reportAway(qint64 since, qint64 until);
    // "keep" (count the time), "trim" (stop as of when you left),
    // "trimRestart" (stop as of then and start a new session now).
    Q_INVOKABLE void resolveAway(const QString &choice);

    // --- other apps: D-Bus, KRunner, MCP (appcontroller_external.cpp) --------
    // Open (or done/all) tasks matching `search`, as plain maps.
    QVariantList externalTasks(const QString &filter, const QString &search) const;
    bool externalWritesAllowed() const;
    // KRunner: "tt <text>" offers to capture it; three or more letters find
    // open tasks to open, start or finish. Ids: "add:<text>", "task:<id>".
    QVariantList runnerMatches(const QString &query) const;
    // actionId: "" (default: capture / open), "start", "done".
    bool runRunnerMatch(const QString &matchId, const QString &actionId);

    static QStringList splitCommand(const QString &line);
    static QString commandHash(const QString &argvJson);

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

    // Changes from a sync: applied and shown, but not on the undo stack
    // (undoing them would just be redone by the next sync).
    bool applyExternal(const Mutation &mutation, QString *error = nullptr);
    // The name of the subscribed calendar an event comes from, if any.
    QString feedNameOf(const QString &eventId) const;

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
    void planReady(const QVariantMap &plan);
    void stepsReady(const QString &taskId, const QVariantList &steps, const QString &explanation);
    void aiCaptureReady(const QString &text, const QVariantMap &preview);
    void aiFailed(const QString &feature, const QString &error);
    void aiChanged();
    void energyChanged();
    void ollamaModelsChanged();
    void awayChanged();
    void focusModeChanged();
    // For the launcher: open this (url/file/app) or run this approved command.
    void launchRequested(const QVariantMap &action);
    void commandNeedsApproval(const QString &actionId, const QString &taskTitle, const QString &command);
    void workStarted(const QString &taskId);
    void openTaskRequested(const QString &taskId);

private:
    void refresh(const QSet<QString> &tables);
    void refreshToday();
    void ensureOccurrences();
    QString resolveCategory(const QString &name) const;
    QString createFromCapture(const CaptureResult &r, const QString &source);
    QVariantMap previewOf(const CaptureResult &r) const;
    void refreshEnergy();
    void initAi();
    void launchForTask(const QString &taskId);
    void launch(const TaskAction &action, bool askIfNeeded);
    QVariantMap actionToVariant(const TaskAction &a) const;

    // AI plumbing (appcontroller_ai.cpp)
    ai::Provider *provider();
    void setAiBusy(bool busy);
    QString logAi(const QString &feature, const ai::Result &result, const QString &prompt);
    void setAiFeedback(const QString &logId, const QString &feedback);
    PlanOptions planOptions(QDate day) const;
    // How long things usually take, from the last 90 days' finished tasks;
    // empty when Settings > "Plan with your real pace" is off.
    stats::Pace pace() const;
    // A new block's length for a task: its estimate at your pace.
    int plannedMinutes(const Task &task) const;
    QVariantMap planToVariant(const DayPlan &plan, const QString &aiError, const ai::Result *result, const QString &logId) const;
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

    QNetworkAccessManager m_network;
    KeyStore *m_keys;
    QString m_apiKey;
    bool m_aiBusy = false;
    bool m_keyRequested = false;
    QStringList m_ollamaModels;
    std::unique_ptr<ai::Provider> m_provider;
    QString m_providerSignature;
    QVariantMap m_energy;
    QVariantMap m_away;
    bool m_focusMode = false;
    QString m_aiCaptureText;
    CaptureResult m_aiCaptureResult;
};

} // namespace tumbler
