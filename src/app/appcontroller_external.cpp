// AppController: what other programs see and may do. The D-Bus API, the
// KRunner plugin and the MCP server (through D-Bus) all come through here.

#include "appcontroller.h"
#include "captureparser.h"
#include "dependencygraph.h"
#include "variants.h"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

namespace tumbler {

bool AppController::externalWritesAllowed() const
{
    return settings().value(QStringLiteral("externalWrites"), true).toBool();
}

QVariantList AppController::externalTasks(const QString &filter, const QString &search) const
{
    const Queries q = queries();
    QHash<QString, Task> all;
    for (const Task &t : q.tasks())
        all.insert(t.id, t);
    DependencyGraph graph;
    for (const Dependency &d : q.dependencies())
        graph.add(d.taskId, d.dependsOnId);
    QHash<QString, QString> categories;
    for (const Category &c : q.categories())
        categories.insert(c.id, c.name);
    QHash<QString, qint64> nextBlock;
    for (const ScheduleBlock &s : q.schedules(now(), std::numeric_limits<qint64>::max())) {
        if (!s.completed && !nextBlock.contains(s.taskId))
            nextBlock.insert(s.taskId, s.start);
    }
    const auto statusOf = [&all](const QString &id) { return all.value(id).status; };

    QVariantList out;
    for (const Task &t : std::as_const(all)) {
        const bool finished = isFinished(t.status);
        if ((filter == QLatin1String("done") && !finished) || (filter != QLatin1String("done") && filter != QLatin1String("all") && finished))
            continue;
        if (!search.isEmpty() && !t.title.contains(search, Qt::CaseInsensitive) && !t.tags.contains(search, Qt::CaseInsensitive))
            continue;
        QVariantMap m = toVariant(t);
        m.insert(QStringLiteral("category"), categories.value(t.categoryId));
        m.insert(QStringLiteral("blocked"), !finished && !graph.blockers(t.id, statusOf).isEmpty());
        if (nextBlock.contains(t.id))
            m.insert(QStringLiteral("nextBlock"), QDateTime::fromMSecsSinceEpoch(nextBlock.value(t.id), zone()).toString(Qt::ISODate));
        if (!t.parentId.isEmpty())
            m.insert(QStringLiteral("parentTitle"), all.value(t.parentId).title);
        // Keep the payload small and readable for agents.
        for (const char *key : {"createdAt", "color", "fixedTime", "recurrence", "complexity"})
            m.remove(QLatin1String(key));
        out.append(m);
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("title")).toString() < b.toMap().value(QStringLiteral("title")).toString();
    });
    return out;
}

QVariantList AppController::runnerMatches(const QString &query) const
{
    QVariantList out;
    const QString q = query.trimmed();
    static const QRegularExpression prefix(QStringLiteral(R"(^(?:tt|task|todo)\s+(.*)$)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = prefix.match(q);
    if (m.hasMatch() && !m.captured(1).trimmed().isEmpty()) {
        const QString text = m.captured(1).trimmed();
        const QVariantMap p = previewCapture(text);
        QStringList details;
        for (const char *key : {"date", "time", "recurrence"}) {
            if (p.contains(QLatin1String(key)))
                details.append(p.value(QLatin1String(key)).toString());
        }
        if (p.contains(QStringLiteral("estimate")))
            details.append(QStringLiteral("%1 min").arg(p.value(QStringLiteral("estimate")).toInt()));
        for (const QString &tag : p.value(QStringLiteral("tags")).toStringList())
            details.append(QLatin1Char('#') + tag);
        out.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("add:") + text},
            {QStringLiteral("text"), QStringLiteral("Add task: %1").arg(p.value(QStringLiteral("title")).toString())},
            {QStringLiteral("subtext"), details.join(QStringLiteral(" · "))},
            {QStringLiteral("icon"), QStringLiteral("list-add")},
            {QStringLiteral("relevance"), 1.0},
            {QStringLiteral("exact"), true},
        });
        return out;
    }

    if (q.size() < 3)
        return out;
    int found = 0;
    for (const QVariant &v : externalTasks(QStringLiteral("open"), q)) {
        const QVariantMap t = v.toMap();
        const QString title = t.value(QStringLiteral("title")).toString();
        QString subtext = t.value(QStringLiteral("status")).toString() == QLatin1String("in_progress") ? QStringLiteral("In progress")
                                                                                                       : QStringLiteral("Task");
        if (t.contains(QStringLiteral("nextBlock")))
            subtext += QStringLiteral(" · scheduled %1").arg(QLocale().toString(
                QDateTime::fromString(t.value(QStringLiteral("nextBlock")).toString(), Qt::ISODate).time(), QLocale::ShortFormat));
        out.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("task:") + t.value(QStringLiteral("id")).toString()},
            {QStringLiteral("text"), title},
            {QStringLiteral("subtext"), subtext},
            {QStringLiteral("icon"), QStringLiteral("view-task")},
            // Titles that start with the query rank above ones that only contain it.
            {QStringLiteral("relevance"), title.startsWith(q, Qt::CaseInsensitive) ? 0.8 : 0.6},
            {QStringLiteral("exact"), false},
        });
        if (++found == 5)
            break;
    }
    return out;
}

bool AppController::runRunnerMatch(const QString &matchId, const QString &actionId)
{
    if (matchId.startsWith(QLatin1String("add:"))) {
        if (!externalWritesAllowed())
            return false;
        return !capture(matchId.mid(4)).isEmpty();
    }
    if (!matchId.startsWith(QLatin1String("task:")))
        return false;
    const QString id = matchId.mid(5);
    if (actionId.isEmpty()) {
        Q_EMIT showWindowRequested();
        Q_EMIT openTaskRequested(id);
        return true;
    }
    if (!externalWritesAllowed())
        return false;
    if (actionId == QLatin1String("start"))
        return startWork(id);
    if (actionId == QLatin1String("done"))
        return setTaskStatus(id, QStringLiteral("completed"));
    return false;
}

} // namespace tumbler
