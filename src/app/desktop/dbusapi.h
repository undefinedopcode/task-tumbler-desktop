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

private:
    bool mayWrite();
    AppController *m_app;
};

} // namespace tumbler
