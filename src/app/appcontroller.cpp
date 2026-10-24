#include "appcontroller.h"

#include "captureparser.h"
#include "dependencygraph.h"
#include "importer.h"
#include "operations.h"
#include "planner.h"
#include "provider.h"
#include "tasklistmodel.h"
#include "today.h"
#include "variants.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUndoCommand>
#include <QUrl>

namespace tumbler {

namespace {

AppController *s_instance = nullptr;

constexpr qint64 kMinute = 60 * 1000;

// Wraps an already-applied mutation. The first redo() is the push itself and
// does nothing; later ones replay the after-state.
class MutationCommand : public QUndoCommand
{
public:
    MutationCommand(AppController *app, AppliedMutation applied)
        : QUndoCommand(applied.text)
        , m_app(app)
        , m_applied(std::move(applied))
    {
    }

    void undo() override { m_app->revertApplied(m_applied); }
    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        m_app->reapplyApplied(m_applied);
    }

private:
    AppController *m_app;
    AppliedMutation m_applied;
    bool m_first = true;
};

QString formatTime(qint64 ms, const QTimeZone &zone)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms, zone);
    const QDate today = QDateTime::currentDateTime(zone).date();
    const QString time = QLocale().toString(dt.time(), QLocale::ShortFormat);
    if (dt.date() == today)
        return time;
    if (dt.date() == today.addDays(1))
        return QStringLiteral("tomorrow %1").arg(time);
    return QStringLiteral("%1 %2").arg(QLocale().toString(dt.date(), QStringLiteral("ddd d MMM")), time);
}

} // namespace

AppController::AppController(Database &db, QObject *parent, KeyStore *keys)
    : QObject(parent)
    , m_db(db)
    , m_tasks(new TaskListModel(this))
    , m_keys(keys)
{
    m_undo.setUndoLimit(200);
    connect(&m_undo, &QUndoStack::indexChanged, this, &AppController::undoStateChanged);
    connect(&m_undo, &QUndoStack::canUndoChanged, this, &AppController::undoStateChanged);
    connect(&m_undo, &QUndoStack::canRedoChanged, this, &AppController::undoStateChanged);

    // Re-evaluate Today every 30 s: blocks move from upcoming to now to
    // overdue as time passes, and midnight rolls the date over.
    m_tick.setInterval(30 * 1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (QDateTime::currentDateTime(zone()).date() != m_lastTodayDate)
            ensureOccurrences();
        refreshToday();
        refreshEnergy();
    });
    m_tick.start();

    ensureOccurrences();
    refresh({QStringLiteral("tasks"), QStringLiteral("categories"), QStringLiteral("settings")});
    refreshEnergy();
    initAi();
}

AppController::~AppController() = default; // out of line: ai::Provider is incomplete in the header

AppController *AppController::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void AppController::setInstance(AppController *instance)
{
    s_instance = instance;
}

QTimeZone AppController::zone() const
{
    // Local time is wherever the machine is: travelling moves the day with you.
    return QTimeZone::systemTimeZone();
}

qint64 AppController::now() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool AppController::needsOnboarding() const
{
    return queries().isEmpty() && !queries().setting(QStringLiteral("onboarded"), false).toBool();
}

// ---- applying changes ---------------------------------------------------------

bool AppController::apply(const std::optional<Mutation> &mutation, const QString &error)
{
    if (!mutation) {
        if (!error.isEmpty())
            Q_EMIT message(error);
        return false;
    }
    MutationRunner runner(m_db.handle());
    QString runError;
    auto applied = runner.run(*mutation, &runError);
    if (!applied) {
        qWarning() << "mutation failed:" << mutation->text << runError;
        Q_EMIT message(QStringLiteral("Couldn't save that: %1").arg(runError));
        return false;
    }
    const QSet<QString> tables = applied->tables();
    if (!applied->isNoop())
        m_undo.push(new MutationCommand(this, std::move(*applied)));
    refresh(tables);
    return true;
}

bool AppController::revertApplied(const AppliedMutation &applied)
{
    QString error;
    if (!MutationRunner(m_db.handle()).revert(applied, &error)) {
        // The database moved on in a way undo can't follow; drop history
        // rather than risk replaying stale states.
        qWarning() << "undo failed:" << error;
        Q_EMIT message(QStringLiteral("Undo failed: %1").arg(error));
        QMetaObject::invokeMethod(&m_undo, &QUndoStack::clear, Qt::QueuedConnection);
        return false;
    }
    refresh(applied.tables());
    return true;
}

bool AppController::reapplyApplied(const AppliedMutation &applied)
{
    QString error;
    if (!MutationRunner(m_db.handle()).reapply(applied, &error)) {
        qWarning() << "redo failed:" << error;
        Q_EMIT message(QStringLiteral("Redo failed: %1").arg(error));
        QMetaObject::invokeMethod(&m_undo, &QUndoStack::clear, Qt::QueuedConnection);
        return false;
    }
    refresh(applied.tables());
    return true;
}

void AppController::undo()
{
    if (m_undo.canUndo()) {
        const QString text = m_undo.undoText();
        m_undo.undo();
        Q_EMIT message(QStringLiteral("Undid: %1").arg(text));
    }
}

void AppController::redo()
{
    if (m_undo.canRedo())
        m_undo.redo();
}

void AppController::refresh(const QSet<QString> &tables)
{
    const Queries q = queries();
    if (tables.contains(QStringLiteral("tasks")) || tables.contains(QStringLiteral("task_dependencies"))
        || tables.contains(QStringLiteral("schedules")) || tables.contains(QStringLiteral("categories"))
        || tables.contains(QStringLiteral("work_logs"))) {
        if (tables.contains(QStringLiteral("tasks")))
            ensureOccurrences();
        m_tasks->reload(q, now());
    }
    if (tables.contains(QStringLiteral("categories"))) {
        m_categories.clear();
        for (const Category &c : q.categories())
            m_categories.append(toVariant(c));
        Q_EMIT categoriesChanged();
    }
    if (tables.contains(QStringLiteral("settings"))) {
        m_settings = QVariantMap{
            {QStringLiteral("reminderMinutes"), q.setting(QStringLiteral("reminder_minutes"), 5).toInt()},
            {QStringLiteral("notifications"), q.setting(QStringLiteral("notifications"), true).toBool()},
            {QStringLiteral("closeToTray"), q.setting(QStringLiteral("close_to_tray"), true).toBool()},
            {QStringLiteral("autostart"), q.setting(QStringLiteral("autostart"), false).toBool()},
            {QStringLiteral("peakEnergyStart"), q.setting(QStringLiteral("peak_energy_start"), QStringLiteral("09:00")).toString()},
            {QStringLiteral("peakEnergyEnd"), q.setting(QStringLiteral("peak_energy_end"), QStringLiteral("12:00")).toString()},
            {QStringLiteral("breakMinutes"), q.setting(QStringLiteral("break_duration_minutes"), 15).toInt()},
            {QStringLiteral("aiHints"), q.setting(QStringLiteral("ai_scheduling_hints"), QString()).toString()},
            {QStringLiteral("idleMinutes"), q.setting(QStringLiteral("idle_minutes"), 10).toInt()},
            {QStringLiteral("externalWrites"), q.setting(QStringLiteral("external_writes"), true).toBool()},
            {QStringLiteral("krunner"), q.setting(QStringLiteral("krunner"), false).toBool()},
        };
        Q_EMIT settingsChanged();
        Q_EMIT onboardingChanged();
    }
    if (tables.contains(QStringLiteral("energy_logs")) || tables.contains(QStringLiteral("settings")))
        refreshEnergy();
    if (tables.contains(QStringLiteral("settings"))) {
        initAi(); // reads the key the first time Claude is chosen
        Q_EMIT aiChanged();
    }
    QStringList list(tables.begin(), tables.end());
    Q_EMIT changed(list);
    refreshToday();
}

void AppController::refreshToday()
{
    const TodayView v = buildToday(queries(), now(), zone());
    m_lastTodayDate = v.date;

    QVariantList ready;
    for (const ReadyTask &r : v.ready) {
        QVariantMap m = toVariant(r.task);
        m.insert(QStringLiteral("subtasksDone"), r.subtasksDone);
        m.insert(QStringLiteral("subtasksTotal"), r.subtasksTotal);
        ready.append(m);
    }

    m_today = QVariantMap{
        {QStringLiteral("date"), v.date.toString(Qt::ISODate)},
        {QStringLiteral("dateLabel"), QLocale().toString(v.date, QStringLiteral("dddd d MMMM"))},
        {QStringLiteral("allDay"), toVariantList(v.allDay)},
        {QStringLiteral("now"), toVariantList(v.now)},
        {QStringLiteral("upcoming"), toVariantList(v.upcoming)},
        {QStringLiteral("overdue"), toVariantList(v.overdue)},
        {QStringLiteral("done"), toVariantList(v.done)},
        {QStringLiteral("ready"), ready},
        {QStringLiteral("readyTotal"), v.readyTotal},
    };
    Q_EMIT todayChanged();

    QVariantMap working;
    if (v.working) {
        working = toVariant(*v.working);
        working.insert(QStringLiteral("title"), v.workingTitle);
    }
    if (working != m_working) {
        m_working = working;
        Q_EMIT workingChanged();
    }
}

void AppController::ensureOccurrences()
{
    // Keep recurring items generated two weeks ahead.
    const QDate today = QDateTime::currentDateTime(zone()).date();
    materializeOccurrences(m_db.handle(), queries().tasks(), today.addDays(-1), today.addDays(14), now());
    m_occurrencesThrough = today.addDays(14);
}

QString AppController::resolveCategory(const QString &name) const
{
    if (name.isEmpty())
        return {};
    for (const Category &c : queries().categories()) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0 || c.name.startsWith(name, Qt::CaseInsensitive))
            return c.id;
    }
    return {};
}

// ---- capture -------------------------------------------------------------------

QVariantMap AppController::previewCapture(const QString &text) const
{
    if (!m_aiCaptureText.isEmpty() && text == m_aiCaptureText) {
        QVariantMap m = previewOf(m_aiCaptureResult);
        m.insert(QStringLiteral("fromAi"), true);
        return m;
    }
    return previewOf(parseCapture(text, QDateTime::currentDateTime(zone()).date()));
}

QVariantMap AppController::previewOf(const CaptureResult &r) const
{
    QVariantMap m{{QStringLiteral("title"), r.title}, {QStringLiteral("tags"), r.tags}};
    if (r.priority)
        m.insert(QStringLiteral("priority"), nameOf(*r.priority));
    if (r.energy)
        m.insert(QStringLiteral("energy"), nameOf(*r.energy));
    if (r.estimateMinutes)
        m.insert(QStringLiteral("estimate"), *r.estimateMinutes);
    if (r.date)
        m.insert(QStringLiteral("date"), QLocale().toString(*r.date, QStringLiteral("ddd d MMM")));
    if (r.time)
        m.insert(QStringLiteral("time"), QLocale().toString(*r.time, QLocale::ShortFormat));
    if (r.recurrence)
        m.insert(QStringLiteral("recurrence"), describeRecurrence(*r.recurrence));
    if (!r.category.isEmpty()) {
        const QString id = resolveCategory(r.category);
        QString name = r.category;
        for (const Category &c : queries().categories()) {
            if (c.id == id)
                name = c.name;
        }
        m.insert(QStringLiteral("category"), name);
        m.insert(QStringLiteral("categoryKnown"), !id.isEmpty());
    }
    return m;
}

QString AppController::capture(const QString &text)
{
    // Use the AI's reading when one was asked for on this exact text.
    if (!m_aiCaptureText.isEmpty() && text == m_aiCaptureText) {
        const CaptureResult r = m_aiCaptureResult;
        m_aiCaptureText.clear();
        return createFromCapture(r, text);
    }
    return createFromCapture(parseCapture(text, QDateTime::currentDateTime(zone()).date()), text);
}

QString AppController::createFromCapture(const CaptureResult &r, const QString &)
{
    const QDate today = QDateTime::currentDateTime(zone()).date();
    if (r.title.isEmpty()) {
        Q_EMIT message(QStringLiteral("Type what needs doing first"));
        return {};
    }

    Task t;
    t.id = newId();
    t.title = r.title;
    t.priority = r.priority.value_or(Priority::Medium);
    t.energy = r.energy.value_or(Energy::Medium);
    t.estimateMinutes = r.estimateMinutes.value_or(30);
    t.tags = r.tags;
    t.categoryId = resolveCategory(r.category);
    if (t.categoryId.isEmpty())
        t.categoryId = ensureDefaultCategory();
    t.recurrence = r.recurrence;
    if (r.recurrence && r.time) {
        t.scheduledTime = *r.time;
        t.fixedTime = true;
    } else if (r.date) {
        t.dueDate = *r.date;
    }

    QList<Mutation> parts{ops::createTask(t, now())};
    QString scheduled;
    // A one-off with a time of day becomes a block at that time.
    if (!r.recurrence && r.time) {
        const QDate day = r.date.value_or(today);
        const qint64 start = QDateTime(day, *r.time, zone()).toMSecsSinceEpoch();
        parts.append(ops::scheduleTask(newId(), t.id, start, start + qint64(t.estimateMinutes) * kMinute, now()));
        scheduled = formatTime(start, zone());
    }
    if (!apply(combine(QStringLiteral("Capture “%1”").arg(t.title), parts)))
        return {};
    Q_EMIT message(scheduled.isEmpty() ? QStringLiteral("Added “%1”").arg(t.title)
                                       : QStringLiteral("Added “%1” for %2").arg(t.title, scheduled));
    return t.id;
}

// ---- tasks ----------------------------------------------------------------------

QString AppController::createTask(const QVariantMap &fields)
{
    Task t;
    t.id = newId();
    t.title = fields.value(QStringLiteral("title")).toString().trimmed();
    if (t.title.isEmpty()) {
        Q_EMIT message(QStringLiteral("A task needs a title"));
        return {};
    }
    t.parentId = fields.value(QStringLiteral("parent_task_id")).toString();
    t.categoryId = ensureDefaultCategory();
    if (!t.parentId.isEmpty()) {
        // Subtasks inherit their parent's category.
        if (const auto parent = queries().task(t.parentId))
            t.categoryId = parent->categoryId;
    }

    QVariantMap rest = fields;
    rest.remove(QStringLiteral("title"));
    rest.remove(QStringLiteral("parent_task_id"));

    QList<Mutation> parts{ops::createTask(t, now())};
    if (!rest.isEmpty()) {
        // Build the edit against a database that already holds the new task:
        // run the creation first, then fold the edit into the same undo step.
        Mutation edit;
        edit.text = QString();
        edit.scopes = {{QStringLiteral("tasks"), QStringLiteral("id = ?"), {t.id}}};
        const qint64 ts = now();
        const QString id = t.id;
        edit.apply = [rest, id, ts](QSqlDatabase db, QString *error) {
            const auto m = ops::updateTask(Queries(db), id, rest, ts, error);
            return m && m->apply(db, error);
        };
        parts.append(edit);
    }
    if (!apply(combine(QStringLiteral("Add “%1”").arg(t.title), parts)))
        return {};
    return t.id;
}

bool AppController::updateTask(const QString &id, const QVariantMap &fields)
{
    QString error;
    auto m = ops::updateTask(queries(), id, fields, now(), &error);
    return apply(m, error);
}

bool AppController::setTaskStatus(const QString &id, const QString &status)
{
    const auto s = taskStatusFromString(status);
    if (!s)
        return false;
    QString error;
    auto m = ops::setTaskStatus(queries(), id, *s, now(), &error);
    const bool ok = apply(m, error);
    if (ok && *s == TaskStatus::Completed) {
        if (const auto t = queries().task(id))
            Q_EMIT message(QStringLiteral("Done: %1").arg(t->title));
    }
    return ok;
}

bool AppController::deleteTask(const QString &id)
{
    const auto t = queries().task(id);
    if (!t)
        return false;
    Mutation m = ops::deleteTask(id);
    m.text = QStringLiteral("Delete “%1”").arg(t->title);
    const bool ok = apply(m);
    if (ok)
        Q_EMIT message(QStringLiteral("Deleted “%1” (Ctrl+Z to undo)").arg(t->title));
    return ok;
}

QString AppController::addSubtask(const QString &parentId, const QString &title)
{
    return createTask({{QStringLiteral("title"), title}, {QStringLiteral("parent_task_id"), parentId}});
}

bool AppController::addDependency(const QString &taskId, const QString &prerequisiteId)
{
    QString error;
    auto m = ops::addDependency(queries(), taskId, prerequisiteId, now(), &error);
    return apply(m, error);
}

bool AppController::removeDependency(const QString &taskId, const QString &prerequisiteId)
{
    return apply(ops::removeDependency(taskId, prerequisiteId));
}

QVariantMap AppController::taskDetail(const QString &id) const
{
    const Queries q = queries();
    const auto task = q.task(id);
    if (!task)
        return {};

    QHash<QString, Task> all;
    for (const Task &t : q.tasks())
        all.insert(t.id, t);

    QVariantMap m = toVariant(*task);

    QVariantList subtasks;
    for (const Task &t : std::as_const(all)) {
        if (t.parentId == id)
            subtasks.append(toVariant(t));
    }
    std::sort(subtasks.begin(), subtasks.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("createdAt")).toLongLong() < b.toMap().value(QStringLiteral("createdAt")).toLongLong();
    });
    m.insert(QStringLiteral("subtasks"), subtasks);

    QVariantList prerequisites, dependents;
    for (const Dependency &d : q.dependencies()) {
        if (d.taskId == id && all.contains(d.dependsOnId))
            prerequisites.append(toVariant(all.value(d.dependsOnId)));
        if (d.dependsOnId == id && all.contains(d.taskId))
            dependents.append(toVariant(all.value(d.taskId)));
    }
    m.insert(QStringLiteral("prerequisites"), prerequisites);
    m.insert(QStringLiteral("dependents"), dependents);

    if (!task->parentId.isEmpty() && all.contains(task->parentId))
        m.insert(QStringLiteral("parentTitle"), all.value(task->parentId).title);

    QVariantList blocks;
    const qint64 t0 = now();
    for (const ScheduleBlock &s : q.schedulesForTask(id)) {
        if (s.end >= t0 - 24 * 60 * kMinute || !s.completed) {
            QVariantMap b = toVariant(s);
            b.insert(QStringLiteral("label"), formatTime(s.start, zone()));
            blocks.append(b);
        }
    }
    m.insert(QStringLiteral("schedules"), blocks);

    QVariantList logs;
    for (const WorkLog &w : q.workLogsForTask(id))
        logs.append(toVariant(w));
    m.insert(QStringLiteral("workLogs"), logs);
    m.insert(QStringLiteral("loggedMinutes"), q.loggedMinutes().value(id));

    QVariantList actions;
    for (const TaskAction &a : q.actions(id))
        actions.append(actionToVariant(a));
    m.insert(QStringLiteral("actions"), actions);

    for (const Category &c : q.categories()) {
        if (c.id == task->categoryId) {
            m.insert(QStringLiteral("categoryName"), c.name);
            m.insert(QStringLiteral("categoryColor"), c.color);
        }
    }
    return m;
}

QVariantList AppController::prerequisiteCandidates(const QString &taskId) const
{
    const Queries q = queries();
    DependencyGraph graph;
    for (const Dependency &d : q.dependencies())
        graph.add(d.taskId, d.dependsOnId);

    QVariantList out;
    for (const Task &t : q.tasks()) {
        if (t.id == taskId || isFinished(t.status) || graph.prerequisitesOf(taskId).contains(t.id)
            || graph.wouldCreateCycle(taskId, t.id))
            continue;
        out.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("title"), t.title}});
    }
    return out;
}

// ---- scheduling -----------------------------------------------------------------

bool AppController::scheduleNextSlot(const QString &taskId)
{
    const Queries q = queries();
    const auto task = q.task(taskId);
    if (!task)
        return false;
    const auto slot = nextFreeSlot(q, *task, now(), zone());
    if (!slot) {
        Q_EMIT message(QStringLiteral("No free slot in the next two weeks within its category's hours"));
        return false;
    }
    Mutation m = ops::scheduleTask(newId(), taskId, slot->start, slot->end, now());
    m.text = QStringLiteral("Schedule “%1”").arg(task->title);
    const bool ok = apply(m);
    if (ok)
        Q_EMIT message(QStringLiteral("“%1” scheduled for %2").arg(task->title, formatTime(slot->start, zone())));
    return ok;
}

bool AppController::scheduleAt(const QString &taskId, qint64 start, int minutes)
{
    const auto task = queries().task(taskId);
    if (!task)
        return false;
    const int duration = minutes > 0 ? minutes : task->estimateMinutes;
    Mutation m = ops::scheduleTask(newId(), taskId, start, start + qint64(duration) * kMinute, now());
    m.text = QStringLiteral("Schedule “%1”").arg(task->title);
    return apply(m);
}

bool AppController::moveSchedule(const QString &id, qint64 start)
{
    const auto block = queries().schedule(id);
    if (!block)
        return false;
    QString error;
    return apply(ops::moveSchedule(queries(), id, start, start + (block->end - block->start), false, now(), &error), error);
}

bool AppController::bump(const QString &scheduleId, int minutes)
{
    const auto block = queries().schedule(scheduleId);
    if (!block)
        return false;
    // Bumping something already past starts it from now, not from its old slot.
    const qint64 base = std::max(block->start, now());
    const qint64 start = base + qint64(minutes) * kMinute;
    QString error;
    const bool ok = apply(ops::moveSchedule(queries(), scheduleId, start, start + (block->end - block->start), true, now(), &error),
                          error);
    if (ok)
        noteMove(scheduleId);
    return ok;
}

bool AppController::bumpToNextSlot(const QString &scheduleId)
{
    const Queries q = queries();
    const auto block = q.schedule(scheduleId);
    const auto task = block ? q.task(block->taskId) : std::nullopt;
    if (!task)
        return false;
    Task sized = *task;
    sized.estimateMinutes = int((block->end - block->start) / kMinute);
    const auto slot = nextFreeSlot(q, sized, std::max(now(), block->end), zone(), 14, scheduleId);
    if (!slot) {
        Q_EMIT message(QStringLiteral("No free slot in the next two weeks"));
        return false;
    }
    QString error;
    const bool ok = apply(ops::moveSchedule(q, scheduleId, slot->start, slot->end, true, now(), &error), error);
    if (ok)
        noteMove(scheduleId);
    return ok;
}

bool AppController::bumpToTomorrow(const QString &scheduleId)
{
    const Queries q = queries();
    const auto block = q.schedule(scheduleId);
    const auto task = block ? q.task(block->taskId) : std::nullopt;
    if (!task)
        return false;
    Task sized = *task;
    sized.estimateMinutes = int((block->end - block->start) / kMinute);
    const QDate tomorrow = QDateTime::currentDateTime(zone()).date().addDays(1);
    const qint64 from = QDateTime(tomorrow, QTime(0, 0), zone()).toMSecsSinceEpoch();
    const auto slot = nextFreeSlot(q, sized, from, zone(), 14, scheduleId);
    if (!slot) {
        Q_EMIT message(QStringLiteral("No free slot in the next two weeks"));
        return false;
    }
    QString error;
    const bool ok = apply(ops::moveSchedule(q, scheduleId, slot->start, slot->end, true, now(), &error), error);
    if (ok)
        noteMove(scheduleId);
    return ok;
}

void AppController::noteMove(const QString &scheduleId)
{
    const Queries q = queries();
    const auto block = q.schedule(scheduleId);
    if (!block)
        return;
    QString text = QStringLiteral("Moved to %1").arg(formatTime(block->start, zone()));
    // The web app refused moves past the due date. Being behind is exactly
    // when moving things matters, so allow it and just say so.
    if (const auto task = q.task(block->taskId); task && task->dueDate.isValid()) {
        const QDate endDay = QDateTime::fromMSecsSinceEpoch(block->end, zone()).date();
        if (endDay > task->dueDate)
            text += QStringLiteral(", after its due date (%1)").arg(QLocale().toString(task->dueDate, QStringLiteral("ddd d MMM")));
    }
    Q_EMIT message(text);
}

bool AppController::completeSchedule(const QString &id)
{
    QString error;
    const bool ok = apply(ops::completeSchedule(queries(), id, now(), &error), error);
    if (ok)
        Q_EMIT message(QStringLiteral("Nice. Done."));
    return ok;
}

bool AppController::unschedule(const QString &id)
{
    return apply(ops::deleteSchedule(id));
}

bool AppController::setOccurrence(const QString &id, const QString &status)
{
    const auto s = occurrenceStatusFromString(status);
    if (!s)
        return false;
    QString error;
    return apply(ops::setOccurrenceStatus(queries(), id, *s, now(), &error), error);
}

QVariantMap AppController::dayAgenda(const QString &isoDate) const
{
    const QDate day = QDate::fromString(isoDate, Qt::ISODate);
    if (!day.isValid())
        return {};
    const QTimeZone z = zone();
    const qint64 dayStart = QDateTime(day, QTime(0, 0), z).toMSecsSinceEpoch();
    const qint64 dayEnd = QDateTime(day.addDays(1), QTime(0, 0), z).toMSecsSinceEpoch();
    const Queries q = queries();

    // Reuse the Today builder at the given day by evaluating it "at" that day;
    // for the actual today the real clock decides what is now and overdue.
    const bool isToday = day == QDateTime::currentDateTime(z).date();
    const qint64 evalAt = isToday ? now() : (day < QDateTime::currentDateTime(z).date() ? dayEnd - 1 : dayStart);
    const TodayView v = buildToday(q, evalAt, z, 0);

    QVariantList items;
    for (const auto *list : {&v.now, &v.upcoming, &v.overdue, &v.done})
        items += toVariantList(*list);

    QVariantList windows;
    for (const Category &c : q.categories()) {
        if (!c.showOnSchedule && !c.isDefault)
            continue;
        if (const auto w = c.schedule.windowFor(day)) {
            windows.append(QVariantMap{
                {QStringLiteral("name"), c.name},
                {QStringLiteral("color"), c.color},
                {QStringLiteral("start"), QDateTime(day, w->start, z).toMSecsSinceEpoch()},
                {QStringLiteral("end"), QDateTime(day, w->end, z).toMSecsSinceEpoch()},
            });
        }
    }

    return {
        {QStringLiteral("date"), day.toString(Qt::ISODate)},
        {QStringLiteral("label"), QLocale().toString(day, QStringLiteral("dddd d MMMM"))},
        {QStringLiteral("dayStart"), dayStart},
        {QStringLiteral("dayEnd"), dayEnd},
        {QStringLiteral("isToday"), isToday},
        {QStringLiteral("items"), items},
        {QStringLiteral("allDay"), toVariantList(v.allDay)},
        {QStringLiteral("windows"), windows},
    };
}

// ---- work timer -----------------------------------------------------------------

bool AppController::startWork(const QString &taskId)
{
    QString error;
    if (!apply(ops::startWork(queries(), newId(), taskId, now(), &error), error))
        return false;
    Q_EMIT workStarted(taskId);
    launchForTask(taskId);
    return true;
}

bool AppController::stopWork()
{
    const auto active = queries().activeWorkLog();
    QString error;
    const bool ok = apply(ops::stopWork(queries(), now(), &error), error);
    if (ok && active) {
        const int minutes = int((now() - active->startedAt + kMinute / 2) / kMinute);
        Q_EMIT message(QStringLiteral("Logged %1 min").arg(minutes));
    }
    return ok;
}

// ---- categories & settings ------------------------------------------------------

QString AppController::saveCategory(const QVariantMap &v)
{
    Category c;
    c.id = v.value(QStringLiteral("id")).toString();
    const bool isNew = c.id.isEmpty();
    if (isNew)
        c.id = newId();
    c.name = v.value(QStringLiteral("name")).toString().trimmed();
    if (c.name.isEmpty()) {
        Q_EMIT message(QStringLiteral("A category needs a name"));
        return {};
    }
    c.color = v.value(QStringLiteral("color"), QStringLiteral("#6366f1")).toString();
    c.isDefault = v.value(QStringLiteral("isDefault")).toBool() || queries().categories().isEmpty();
    c.showOnSchedule = v.value(QStringLiteral("showOnSchedule")).toBool();
    c.schedule = scheduleFromVariant(v.value(QStringLiteral("days")).toList());
    return apply(ops::saveCategory(c, isNew, now())) ? c.id : QString();
}

bool AppController::deleteCategory(const QString &id)
{
    QString error;
    return apply(ops::deleteCategory(queries(), id, now(), &error), error);
}

bool AppController::setSetting(const QString &key, const QVariant &value)
{
    return apply(ops::setSetting(key, QJsonValue::fromVariant(value)));
}

// ---- data -----------------------------------------------------------------------

QString AppController::importExport(const QUrl &file)
{
    QFile f(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("Couldn't open %1").arg(f.fileName());
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (!doc.isObject())
        return QStringLiteral("That file isn't JSON: %1").arg(parseError.errorString());

    Importer importer(m_db);
    ImportReport report;
    QString error;
    if (!importer.run(doc.object(), &report, &error))
        return QStringLiteral("Import failed, nothing was changed: %1").arg(error);

    // Imports are not undoable; start history from here.
    m_undo.clear();
    apply(ops::setSetting(QStringLiteral("onboarded"), true));
    m_undo.clear();
    ensureOccurrences();
    refresh({QStringLiteral("tasks"), QStringLiteral("categories"), QStringLiteral("settings"), QStringLiteral("schedules")});

    QStringList lines{QStringLiteral("Imported %1 tasks, %2 events and %3 schedule blocks.")
                          .arg(report.imported.value(QStringLiteral("tasks")))
                          .arg(report.imported.value(QStringLiteral("events")))
                          .arg(report.imported.value(QStringLiteral("schedules")))};
    for (const QString &w : report.warnings)
        lines.append(QStringLiteral("• ") + w);
    return lines.join(QLatin1Char('\n'));
}

void AppController::startFresh()
{
    ensureDefaultCategory();
    apply(ops::setSetting(QStringLiteral("onboarded"), true));
    m_undo.clear();
}

QString AppController::ensureDefaultCategory()
{
    const Queries q = queries();
    if (const auto def = q.defaultCategory())
        return def->id;

    Category c;
    c.id = newId();
    c.name = QStringLiteral("Default");
    c.isDefault = true;
    c.showOnSchedule = true;
    c.schedule = CategorySchedule::anytime();
    for (const Category &existing : q.categories()) {
        if (existing.name.compare(c.name, Qt::CaseInsensitive) == 0)
            c.name = QStringLiteral("Anytime");
    }
    // Setup rather than an edit, so it runs outside the undo stack.
    QString error;
    if (!MutationRunner(m_db.handle()).run(ops::saveCategory(c, true, now()), &error)) {
        qWarning() << "creating the default category failed:" << error;
        return {};
    }
    refresh({QStringLiteral("categories")});
    return c.id;
}

QString AppController::backupNow()
{
    const QString dir = QFileInfo(m_db.path()).absolutePath() + QStringLiteral("/backups");
    QDir().mkpath(dir);
    const QString target =
        dir + QStringLiteral("/tumbler-%1.db").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    QString error;
    if (!m_db.backupTo(target, &error)) {
        Q_EMIT message(QStringLiteral("Backup failed: %1").arg(error));
        return {};
    }
    Q_EMIT message(QStringLiteral("Backed up to %1").arg(QFileInfo(target).fileName()));
    return target;
}

void AppController::dailyBackup()
{
    if (m_db.path() == QLatin1String(":memory:") || queries().isEmpty())
        return;
    const QString dir = QFileInfo(m_db.path()).absolutePath() + QStringLiteral("/backups");
    QDir().mkpath(dir);
    const QString target = dir + QStringLiteral("/daily-%1.db").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    if (QFile::exists(target))
        return;
    QString error;
    if (!m_db.backupTo(target, &error)) {
        qWarning() << "daily backup failed:" << error;
        return;
    }
    QDir backups(dir);
    QStringList daily = backups.entryList({QStringLiteral("daily-*.db")}, QDir::Files, QDir::Name);
    while (daily.size() > 14)
        backups.remove(daily.takeFirst());
}

void AppController::openDataFolder() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_db.path()).absolutePath()));
}

} // namespace tumbler
