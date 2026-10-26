#pragma once

#include <QDBusContext>
#include <QObject>

namespace tumbler {

class AppController;

// Task Tumbler on the session bus, for scripts, the CLI and the MCP server:
//   service org.kde.tasktumbler, path /Tasks, interface org.kde.tasktumbler.Tasks
// Complex results are JSON strings, so any language can use them:
//   qdbus org.kde.tasktumbler /Tasks org.kde.tasktumbler.Tasks.Capture "call mum tomorrow"
// Writes go through the same undoable path as the UI, and are refused when
// Settings > "Let other apps change tasks" is off.
class DBusApi : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.tasktumbler.Tasks")

public:
    explicit DBusApi(AppController *app, QObject *parent = nullptr);
    bool registerOnBus();

public Q_SLOTS:
    Q_SCRIPTABLE QString Today();
    // filter: "open" (default), "done", "all"
    Q_SCRIPTABLE QString Tasks(const QString &filter, const QString &search);
    Q_SCRIPTABLE QString Task(const QString &id);
    Q_SCRIPTABLE QString Current();
    // Quick-capture syntax; returns the new task's id.
    Q_SCRIPTABLE QString Capture(const QString &text);
    Q_SCRIPTABLE bool Complete(const QString &id);
    Q_SCRIPTABLE bool StartWork(const QString &id);
    Q_SCRIPTABLE bool StopWork();
    Q_SCRIPTABLE bool ScheduleNextSlot(const QString &id);
    Q_SCRIPTABLE void Show();

    // Notes. search: words to find (prefix match); tag: only notes with it.
    Q_SCRIPTABLE QString Notes(const QString &search, const QString &tag);
    Q_SCRIPTABLE QString Note(const QString &id);
    // tags: comma separated; taskId may be empty. Returns the new note's id.
    Q_SCRIPTABLE QString AddNote(const QString &title, const QString &content, const QString &taskId, const QString &tags);
    Q_SCRIPTABLE bool AppendToNote(const QString &id, const QString &text);

    // Looking back over the last `days` days including today (0: everything):
    // time worked, things finished, busy hours, estimate accuracy.
    Q_SCRIPTABLE QString Stats(int days);

    // Closing out the day: what got done, what's still open, tomorrow at a glance.
    Q_SCRIPTABLE QString CloseOut();
    // kind: "block" or "routine", id: the leftover's id from CloseOut.
    // choice for blocks: "tomorrow", "pile", "drop", "done"; for routines: "done", "skip".
    Q_SCRIPTABLE bool ResolveLeftover(const QString &kind, const QString &id, const QString &choice);
    Q_SCRIPTABLE bool ClearLeftovers();
    // Adds a paragraph to today's day note, starting it if there isn't one. Returns its id.
    Q_SCRIPTABLE QString AddToDayNote(const QString &text);
    Q_SCRIPTABLE bool FinishCloseOut();
    // Cancels a task and takes its blocks off the schedule.
    Q_SCRIPTABLE bool LetGo(const QString &id);

    // The last seven days: what was done, what keeps moving, gone quiet, due soon.
    Q_SCRIPTABLE QString WeekReview();
    Q_SCRIPTABLE bool FinishReview();

private:
    bool mayWrite();
    AppController *m_app;
};

} // namespace tumbler
