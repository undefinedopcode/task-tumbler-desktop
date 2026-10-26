#include "stats.h"

#include <QDateTime>
#include <QHash>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>
#include <cmath>

namespace tumbler::stats {

namespace {

QSqlQuery run(QSqlDatabase db, const QString &sql, const QVariantList &binds)
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    q.exec();
    return q;
}

double median(QList<double> values)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    const qsizetype n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}

struct Sample {
    int estimate;
    int worked;
};

Calibration calibrate(const QString &label, const QList<Sample> &samples)
{
    Calibration c;
    c.label = label;
    c.tasks = int(samples.size());
    QList<double> ratios;
    for (const Sample &s : samples) {
        c.estimatedMinutes += s.estimate;
        c.workedMinutes += s.worked;
        ratios.append(double(s.worked) / s.estimate);
    }
    c.ratio = median(ratios);
    return c;
}

// Estimate buckets, by upper bound in minutes. The label is a key the UI
// words: "15", "30", "60", "120", "longer".
QString sizeOf(int estimate)
{
    for (int bound : {15, 30, 60, 120}) {
        if (estimate <= bound)
            return QString::number(bound);
    }
    return QStringLiteral("longer");
}

struct FinishedTask {
    QString id;
    QString title;
    int estimate;
    QString categoryId;
    qint64 doneAt;
};

// One-off tasks finished in [start, end), by when they were last completed
// (tasks imported without history fall back to their last update).
QList<FinishedTask> finishedTasks(const Queries &q, qint64 start, qint64 end)
{
    QList<FinishedTask> out;
    QSqlQuery f = run(q.db(),
                      QStringLiteral("SELECT t.id, t.title, t.time_estimate_minutes, t.category_id, COALESCE(MAX(h.at), t.updated_at) AS done_at "
                                     "FROM tasks t LEFT JOIN task_history h ON h.task_id = t.id AND h.action = 'completed' "
                                     " AND (h.details IS NULL OR json_extract(h.details, '$.occurrence_date') IS NULL) "
                                     "WHERE t.status = 'completed' AND t.recurrence IS NULL "
                                     "GROUP BY t.id HAVING done_at >= ? AND done_at < ?"),
                      {start, end});
    while (f.next())
        out.append({f.value(0).toString(), f.value(1).toString(), f.value(2).toInt(), f.value(3).toString(), f.value(4).toLongLong()});
    return out;
}

} // namespace

std::optional<QDate> firstActivity(const Queries &q, const QTimeZone &zone)
{
    QSqlQuery r = run(q.db(),
                      QStringLiteral("SELECT MIN(x) FROM ("
                                     " SELECT MIN(started_at) AS x FROM work_logs"
                                     " UNION ALL SELECT MIN(at) FROM task_history"
                                     " UNION ALL SELECT MIN(completed_at) FROM task_occurrences"
                                     " UNION ALL SELECT MIN(logged_at) FROM energy_logs)"),
                      {});
    if (!r.next() || r.value(0).isNull())
        return std::nullopt;
    return QDateTime::fromMSecsSinceEpoch(r.value(0).toLongLong(), zone).date();
}

Summary summarize(const Queries &q, QDate from, QDate to, const QTimeZone &zone)
{
    Summary s;
    s.from = from;
    s.to = std::max(from, to);
    const qint64 start = QDateTime(s.from, QTime(0, 0), zone).toMSecsSinceEpoch();
    const qint64 end = QDateTime(s.to.addDays(1), QTime(0, 0), zone).toMSecsSinceEpoch();

    QHash<QDate, Day> days;
    for (QDate d = s.from; d <= s.to; d = d.addDays(1))
        days.insert(d, Day{d, 0, 0});
    const auto dayOf = [&](qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms, zone).date(); };

    // Time worked. A session is split across the local hours (and days) it
    // ran through, and clipped to the range.
    QHash<QString, TaskTime> perTask;
    QSqlQuery w = run(q.db(),
                      QStringLiteral("SELECT w.task_id, w.started_at, w.duration_minutes, t.title, t.time_estimate_minutes, t.status "
                                     "FROM work_logs w JOIN tasks t ON t.id = w.task_id "
                                     "WHERE w.ended_at IS NOT NULL AND w.duration_minutes > 0 "
                                     "AND w.started_at < ? AND w.started_at + w.duration_minutes * 60000 > ?"),
                      {end, start});
    while (w.next()) {
        const QString taskId = w.value(0).toString();
        const qint64 began = w.value(1).toLongLong();
        const int duration = w.value(2).toInt();
        TaskTime &t = perTask[taskId];
        if (t.taskId.isEmpty()) {
            t.taskId = taskId;
            t.title = w.value(3).toString();
            t.estimateMinutes = w.value(4).toInt();
            t.finished = w.value(5).toString() == QLatin1String("completed");
        }
        if (began >= start) {
            ++t.sessions;
            ++s.sessions;
            s.longestSessionMinutes = std::max(s.longestSessionMinutes, duration);
        }

        const qint64 first = std::max(began, start);
        const qint64 stop = std::min(began + qint64(duration) * 60000, end);
        for (qint64 at = first; at < stop;) {
            const QDateTime local = QDateTime::fromMSecsSinceEpoch(at, zone);
            const qint64 msIntoHour = (qint64(local.time().minute()) * 60 + local.time().second()) * 1000 + local.time().msec();
            const qint64 chunk = std::min(stop - at, 3600000 - msIntoHour);
            // Whole minutes, counted from the session's start so the parts
            // add up to the whole however it's split.
            const int minutes = int((at + chunk - first) / 60000) - int((at - first) / 60000);
            s.minutesByHour[local.time().hour()] += minutes;
            auto day = days.find(local.date());
            if (day != days.end())
                day->workedMinutes += minutes;
            t.workedMinutes += minutes;
            s.workedMinutes += minutes;
            at += chunk;
        }
    }
    for (const TaskTime &t : std::as_const(perTask)) {
        if (t.workedMinutes > 0 || t.sessions > 0)
            s.tasks.append(t);
    }
    std::sort(s.tasks.begin(), s.tasks.end(), [](const TaskTime &a, const TaskTime &b) {
        return a.workedMinutes != b.workedMinutes ? a.workedMinutes > b.workedMinutes : a.title < b.title;
    });

    // Finished one-off tasks, and how long they took against their estimates.
    const QHash<QString, int> logged = q.loggedMinutes();
    QHash<QString, QString> categoryNames;
    for (const Category &c : q.categories())
        categoryNames.insert(c.id, c.name);
    QList<Sample> all;
    QHash<QString, QList<Sample>> bySize;
    QHash<QString, QList<Sample>> byCategory;
    for (const FinishedTask &f : finishedTasks(q, start, end)) {
        ++s.finishedTasks;
        s.finished.append(Finished{f.id, f.title, f.doneAt, false});
        auto day = days.find(dayOf(f.doneAt));
        if (day != days.end())
            ++day->finished;
        const int worked = logged.value(f.id);
        if (f.estimate <= 0 || worked <= 0)
            continue;
        const Sample sample{f.estimate, worked};
        all.append(sample);
        bySize[sizeOf(f.estimate)].append(sample);
        const QString category = categoryNames.value(f.categoryId);
        if (!category.isEmpty())
            byCategory[category].append(sample);
    }

    QSqlQuery o = run(q.db(),
                      QStringLiteral("SELECT o.completed_at, o.task_id, t.title FROM task_occurrences o JOIN tasks t ON t.id = o.task_id "
                                     "WHERE o.status = 'completed' AND o.completed_at >= ? AND o.completed_at < ?"),
                      {start, end});
    while (o.next()) {
        ++s.finishedRoutines;
        s.finished.append(Finished{o.value(1).toString(), o.value(2).toString(), o.value(0).toLongLong(), true});
        auto day = days.find(dayOf(o.value(0).toLongLong()));
        if (day != days.end())
            ++day->finished;
    }

    s.overall = calibrate({}, all);
    for (const QString &size : {QStringLiteral("15"), QStringLiteral("30"), QStringLiteral("60"), QStringLiteral("120"), QStringLiteral("longer")}) {
        if (bySize.contains(size))
            s.bySize.append(calibrate(size, bySize.value(size)));
    }
    for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it)
        s.byCategory.append(calibrate(it.key(), it.value()));
    std::sort(s.byCategory.begin(), s.byCategory.end(), [](const Calibration &a, const Calibration &b) {
        return a.tasks != b.tasks ? a.tasks > b.tasks : a.label < b.label;
    });

    // Energy check-ins by hour of day.
    std::array<QPair<int, int>, 24> energy{};
    for (const EnergyLog &e : q.energyLogs(start, end)) {
        auto &h = energy[QDateTime::fromMSecsSinceEpoch(e.loggedAt, zone).time().hour()];
        h.first += e.level;
        ++h.second;
    }
    for (int h = 0; h < 24; ++h) {
        if (energy[h].second)
            s.energyByHour[h] = double(energy[h].first) / energy[h].second;
    }

    QSqlQuery m = run(q.db(),
                      QStringLiteral("SELECT t.id, t.title, COUNT(*) AS n FROM task_history h JOIN tasks t ON t.id = h.task_id "
                                     "WHERE h.action = 'bumped' AND h.at >= ? AND h.at < ? AND t.status IN ('pending', 'in_progress') "
                                     "GROUP BY t.id HAVING n >= 2 ORDER BY n DESC, t.title LIMIT 5"),
                      {start, end});
    while (m.next())
        s.moving.append(Moving{m.value(0).toString(), m.value(1).toString(), m.value(2).toInt()});

    std::sort(s.finished.begin(), s.finished.end(), [](const Finished &a, const Finished &b) {
        return a.at != b.at ? a.at < b.at : a.title < b.title;
    });
    for (QDate d = s.from; d <= s.to; d = d.addDays(1))
        s.days.append(days.value(d));
    return s;
}

// ---- pace ---------------------------------------------------------------------

std::optional<Pace::Factor> Pace::factorFor(int estimate, const QString &categoryId) const
{
    // The most specific basis with enough behind it.
    if (const auto c = m_byCategory.constFind(categoryId); !categoryId.isEmpty() && c != m_byCategory.cend())
        return *c;
    if (const auto z = m_bySize.constFind(sizeOf(estimate)); z != m_bySize.cend())
        return *z;
    return m_overall;
}

int Pace::plannedMinutes(int estimate, const QString &categoryId) const
{
    const auto f = factorFor(estimate, categoryId);
    if (!f || f->ratio < kMinRatio)
        return estimate;
    const double stretched = estimate * std::min(f->ratio, kMaxRatio);
    return int(std::ceil(stretched / 5.0)) * 5;
}

Pace learnPace(const Queries &q, qint64 since, qint64 until)
{
    const QHash<QString, int> logged = q.loggedMinutes();
    QList<Sample> all;
    QHash<QString, QList<Sample>> bySize;
    QHash<QString, QList<Sample>> byCategory;
    for (const FinishedTask &f : finishedTasks(q, since, until)) {
        const int worked = logged.value(f.id);
        if (f.estimate <= 0 || worked <= 0)
            continue;
        const Sample sample{f.estimate, worked};
        all.append(sample);
        bySize[sizeOf(f.estimate)].append(sample);
        if (!f.categoryId.isEmpty())
            byCategory[f.categoryId].append(sample);
    }

    Pace pace;
    if (all.size() < Pace::kMinTasks)
        return pace; // too little to go on: plan with the estimates as they are
    const auto factor = [](const QList<Sample> &samples, const QString &basis) {
        return Pace::Factor{calibrate({}, samples).ratio, int(samples.size()), basis};
    };
    pace.m_overall = factor(all, QStringLiteral("overall"));
    for (auto it = bySize.cbegin(); it != bySize.cend(); ++it) {
        if (it.value().size() >= Pace::kMinGroupTasks)
            pace.m_bySize.insert(it.key(), factor(it.value(), QStringLiteral("size")));
    }
    for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it) {
        if (it.value().size() >= Pace::kMinGroupTasks)
            pace.m_byCategory.insert(it.key(), factor(it.value(), QStringLiteral("category")));
    }
    return pace;
}

} // namespace tumbler::stats
