// AppController: closing out the day and the weekly review.

#include "aifeatures.h"
#include "appcontroller.h"
#include "notes.h"
#include "operations.h"
#include "provider.h"
#include "review.h"

#include <QDateTime>
#include <QLocale>
#include <cmath>

namespace tumbler {

namespace {

// Day notes written while closing out carry this tag.
const QString kDayTag = QStringLiteral("day");

QDate localDate(qint64 ms, const QTimeZone &zone)
{
    return QDateTime::fromMSecsSinceEpoch(ms, zone).date();
}

QVariantList winsToVariant(const QList<review::Win> &wins)
{
    QVariantList out;
    for (const review::Win &w : wins) {
        out.append(QVariantMap{{QStringLiteral("taskId"), w.taskId},
                               {QStringLiteral("title"), w.title},
                               {QStringLiteral("at"), w.at},
                               {QStringLiteral("routine"), w.routine},
                               {QStringLiteral("workedMinutes"), w.workedMinutes}});
    }
    return out;
}

QVariantList progressToVariant(const QList<review::Progress> &progress)
{
    QVariantList out;
    for (const review::Progress &p : progress)
        out.append(QVariantMap{{QStringLiteral("taskId"), p.taskId}, {QStringLiteral("title"), p.title}, {QStringLiteral("minutes"), p.minutes}});
    return out;
}

// The day notes written between `from` and `to`, oldest first.
QList<Note> dayNotes(const Queries &q, QDate from, QDate to, const QTimeZone &zone)
{
    QList<Note> out;
    for (const NoteHit &h : notes::find(q, {}, kDayTag)) {
        const QDate d = localDate(h.note.createdAt, zone);
        if (d >= from && d <= to)
            out.prepend(h.note); // find() is newest first
    }
    return out;
}

} // namespace

// ---- when -----------------------------------------------------------------------

bool AppController::closeOutDue() const
{
    const Queries q = queries();
    const QTime from = QTime::fromString(q.setting(QStringLiteral("closeout_time"), QStringLiteral("17:30")).toString(), QStringLiteral("HH:mm"));
    if (!from.isValid()) // "" turns the nudge off
        return false;
    const QDateTime local = QDateTime::fromMSecsSinceEpoch(now(), zone());
    return local.time() >= from && q.setting(QStringLiteral("closeout_last"), QString()).toString() != local.date().toString(Qt::ISODate);
}

bool AppController::reviewDue() const
{
    const Queries q = queries();
    const int weekday = q.setting(QStringLiteral("review_weekday"), 0).toInt(); // 0 = Sunday, -1 = off
    if (weekday < 0 || weekday > 6)
        return false;
    // From the review day until it's done, so a missed day doesn't lose it.
    const QDate today = localDate(now(), zone());
    const QDate reviewDay = today.addDays(-((today.dayOfWeek() % 7 - weekday + 7) % 7));
    return q.setting(QStringLiteral("review_last"), QString()).toString() < reviewDay.toString(Qt::ISODate);
}

// ---- the day --------------------------------------------------------------------

QVariantMap AppController::closeOut() const
{
    const Queries q = queries();
    const QTimeZone z = zone();
    const QDate day = localDate(now(), z);
    const review::DayReview r = review::reviewDay(q, day, z);

    QVariantList leftovers;
    for (const review::Leftover &l : r.leftovers) {
        leftovers.append(QVariantMap{
            {QStringLiteral("kind"), l.kind == review::Leftover::Kind::Block ? QStringLiteral("block") : QStringLiteral("routine")},
            {QStringLiteral("id"), l.id},
            {QStringLiteral("taskId"), l.taskId},
            {QStringLiteral("title"), l.title},
            {QStringLiteral("start"), l.start},
            {QStringLiteral("end"), l.end},
            {QStringLiteral("timeless"), l.timeless},
            {QStringLiteral("earlier"), l.earlier},
            {QStringLiteral("dayLabel"), l.earlier ? QLocale().toString(localDate(l.start, z), QStringLiteral("ddd d MMM")) : QString()},
            {QStringLiteral("bumps"), l.bumps},
        });
    }

    const review::Tomorrow &t = r.tomorrow;
    const QVariantMap tomorrow{
        {QStringLiteral("date"), t.date.toString(Qt::ISODate)},
        {QStringLiteral("label"), QLocale().toString(t.date, QStringLiteral("dddd d MMMM"))},
        {QStringLiteral("blocks"), t.blocks},
        {QStringLiteral("routines"), t.routines},
        {QStringLiteral("events"), t.events},
        {QStringLiteral("firstTitle"), t.firstTitle},
        {QStringLiteral("firstAt"), t.firstAt},
    };

    const QList<Note> notesToday = dayNotes(q, day, day, z);
    return {
        {QStringLiteral("date"), day.toString(Qt::ISODate)},
        {QStringLiteral("dateLabel"), QLocale().toString(day, QStringLiteral("dddd d MMMM"))},
        {QStringLiteral("workedMinutes"), r.workedMinutes},
        {QStringLiteral("wins"), winsToVariant(r.wins)},
        {QStringLiteral("progress"), progressToVariant(r.progress)},
        {QStringLiteral("leftovers"), leftovers},
        {QStringLiteral("tomorrow"), tomorrow},
        {QStringLiteral("noteId"), notesToday.isEmpty() ? QString() : notesToday.last().id},
        {QStringLiteral("note"), notesToday.isEmpty() ? QString() : notesToday.last().content},
        {QStringLiteral("closed"), q.setting(QStringLiteral("closeout_last"), QString()).toString() == day.toString(Qt::ISODate)},
    };
}

bool AppController::resolveLeftover(const QString &kind, const QString &id, const QString &choice)
{
    if (kind == QLatin1String("routine")) {
        if (choice == QLatin1String("done"))
            return setOccurrence(id, QStringLiteral("completed"));
        if (choice == QLatin1String("skip"))
            return setOccurrence(id, QStringLiteral("skipped"));
        return false;
    }

    const Queries q = queries();
    const auto block = q.schedule(id);
    const auto task = block ? q.task(block->taskId) : std::nullopt;
    if (!task)
        return false;
    if (choice == QLatin1String("tomorrow"))
        return bumpToTomorrow(id);
    if (choice == QLatin1String("done"))
        return completeSchedule(id);
    if (choice == QLatin1String("pile")) {
        Mutation m = ops::deleteSchedule(id);
        m.text = QStringLiteral("Back on the pile: “%1”").arg(task->title);
        if (!apply(m))
            return false;
        Q_EMIT message(QStringLiteral("“%1” is back on the pile for another day").arg(task->title));
        return true;
    }
    if (choice == QLatin1String("drop"))
        return letGo(task->id);
    return false;
}

bool AppController::letGo(const QString &taskId)
{
    const Queries q = queries();
    const auto task = q.task(taskId);
    if (!task)
        return false;
    QString error;
    const auto cancel = ops::setTaskStatus(q, taskId, TaskStatus::Cancelled, now(), &error);
    if (!cancel)
        return apply(std::nullopt, error);
    // Its blocks still to do go too, so the schedule doesn't keep a place for it.
    QList<Mutation> parts;
    for (const ScheduleBlock &b : q.schedulesForTask(taskId)) {
        if (!b.completed)
            parts.append(ops::deleteSchedule(b.id));
    }
    parts.append(*cancel);
    if (!apply(combine(QStringLiteral("Let go of “%1”").arg(task->title), parts)))
        return false;
    Q_EMIT message(QStringLiteral("Let go of “%1” (Ctrl+Z to undo)").arg(task->title));
    return true;
}

bool AppController::clearLeftovers()
{
    const Queries q = queries();
    QList<Mutation> parts;
    QString error;
    for (const review::Leftover &l : review::reviewDay(q, localDate(now(), zone()), zone()).leftovers) {
        if (l.kind == review::Leftover::Kind::Block) {
            parts.append(ops::deleteSchedule(l.id));
        } else if (const auto skip = ops::setOccurrenceStatus(q, l.id, OccurrenceStatus::Skipped, now(), &error)) {
            parts.append(*skip);
        }
    }
    if (parts.isEmpty())
        return false;
    if (!apply(combine(QStringLiteral("Clear the rest of the day"), parts)))
        return false;
    Q_EMIT message(QStringLiteral("Cleared. Those tasks are back on the pile (Ctrl+Z to undo)"));
    return true;
}

QString AppController::saveDayNote(const QString &text)
{
    const Queries q = queries();
    const QDate day = localDate(now(), zone());
    const QList<Note> existing = dayNotes(q, day, day, zone());
    Note n;
    if (!existing.isEmpty()) {
        n = existing.last();
    } else {
        if (text.trimmed().isEmpty())
            return {};
        n.id = newId();
        n.title = QLocale().toString(day, QStringLiteral("dddd d MMMM"));
        n.tags = {kDayTag};
    }
    n.content = text;
    // Saved as you type, like the note editor: not undoable keystroke by keystroke.
    QString error;
    if (!applyExternal(notes::save(n, existing.isEmpty(), now()), &error)) {
        Q_EMIT message(QStringLiteral("Couldn't save the note: %1").arg(error));
        return {};
    }
    return n.id;
}

void AppController::finishCloseOut(bool quietly)
{
    applyExternal(ops::setSetting(QStringLiteral("closeout_last"), localDate(now(), zone()).toString(Qt::ISODate)));
    if (!quietly)
        Q_EMIT message(QStringLiteral("Day closed. Rest well."));
}

// ---- the week -------------------------------------------------------------------

QVariantMap AppController::weekReview() const
{
    const Queries q = queries();
    const QTimeZone z = zone();
    const QDate to = localDate(now(), z);
    const QDate from = to.addDays(-6);
    const review::WeekReview w = review::reviewWeek(q, from, to, z);
    const stats::Summary &s = w.summary;

    QVariantList days;
    for (const stats::Day &d : s.days) {
        days.append(QVariantMap{{QStringLiteral("label"), QLocale().toString(d.date, QStringLiteral("ddd"))},
                                {QStringLiteral("workedMinutes"), d.workedMinutes},
                                {QStringLiteral("finished"), d.finished}});
    }
    QVariantList moving;
    for (const stats::Moving &m : s.moving)
        moving.append(QVariantMap{{QStringLiteral("taskId"), m.taskId}, {QStringLiteral("title"), m.title}, {QStringLiteral("bumps"), m.bumps}});
    QVariantList stale;
    for (const review::Stale &t : w.stale)
        stale.append(QVariantMap{{QStringLiteral("taskId"), t.taskId}, {QStringLiteral("title"), t.title}, {QStringLiteral("days"), t.days}});
    QVariantList dueSoon;
    for (const review::DueSoon &d : w.dueSoon) {
        dueSoon.append(QVariantMap{{QStringLiteral("taskId"), d.taskId},
                                   {QStringLiteral("title"), d.title},
                                   {QStringLiteral("due"), d.due.toString(Qt::ISODate)},
                                   {QStringLiteral("overdue"), d.due < to}});
    }
    QVariantList notesOut;
    for (const Note &n : dayNotes(q, from, to, z)) {
        notesOut.append(QVariantMap{{QStringLiteral("id"), n.id},
                                    {QStringLiteral("title"), n.title},
                                    {QStringLiteral("content"), n.content}});
    }

    const QString last = q.setting(QStringLiteral("review_last"), QString()).toString();
    return {
        {QStringLiteral("from"), from.toString(Qt::ISODate)},
        {QStringLiteral("to"), to.toString(Qt::ISODate)},
        {QStringLiteral("rangeLabel"), QStringLiteral("%1 to %2").arg(QLocale().toString(from, QStringLiteral("ddd d MMM")),
                                                                         QLocale().toString(to, QStringLiteral("ddd d MMM")))},
        {QStringLiteral("workedMinutes"), s.workedMinutes},
        {QStringLiteral("sessions"), s.sessions},
        {QStringLiteral("finishedTasks"), s.finishedTasks},
        {QStringLiteral("finishedRoutines"), s.finishedRoutines},
        {QStringLiteral("days"), days},
        {QStringLiteral("wins"), winsToVariant(w.wins)},
        {QStringLiteral("progress"), progressToVariant(w.progress)},
        {QStringLiteral("moving"), moving},
        {QStringLiteral("stale"), stale},
        {QStringLiteral("dueSoon"), dueSoon},
        {QStringLiteral("dayNotes"), notesOut},
        {QStringLiteral("estimateTasks"), s.overall.tasks},
        {QStringLiteral("estimateRatio"), s.overall.ratio},
        {QStringLiteral("doneToday"), last == to.toString(Qt::ISODate)},
    };
}

void AppController::summarizeWeek()
{
    ai::Provider *p = provider();
    if (!p) {
        Q_EMIT aiFailed(QStringLiteral("week"), QStringLiteral("Set up AI in Settings first"));
        return;
    }
    const Queries q = queries();
    const QTimeZone z = zone();
    const QDate to = localDate(now(), z);
    const QDate from = to.addDays(-6);
    const review::WeekReview w = review::reviewWeek(q, from, to, z);
    const QLocale c = QLocale::c(); // the prompt is in English whatever the UI language

    ai::WeekContext context;
    context.range = QStringLiteral("%1 to %2").arg(c.toString(from, QStringLiteral("dddd d MMMM")), c.toString(to, QStringLiteral("dddd d MMMM")));
    context.workedMinutes = w.summary.workedMinutes;
    for (const review::Win &win : w.wins)
        context.finished << (win.routine ? QStringLiteral("%1 (routine)").arg(win.title) : win.title);
    for (const review::Progress &pr : w.progress)
        context.progress << QStringLiteral("%1 (%2 min)").arg(pr.title).arg(pr.minutes);
    for (const stats::Moving &m : w.summary.moving)
        context.moving << QStringLiteral("%1 (moved %2 times)").arg(m.title).arg(m.bumps);
    for (const review::Stale &s : w.stale)
        context.stale << QStringLiteral("%1 (untouched for %2 days)").arg(s.title).arg(s.days);
    for (const review::DueSoon &d : w.dueSoon)
        context.dueSoon << QStringLiteral("%1 (due %2)").arg(d.title, c.toString(d.due, QStringLiteral("dddd d MMMM")));
    if (w.summary.overall.tasks >= 3)
        context.estimates = QStringLiteral("finished tasks took about %1 times their estimates").arg(w.summary.overall.ratio, 0, 'f', 1);
    // Day notes are their own words: only when notes may be shared.
    if (ai().value(QStringLiteral("local")).toBool() || ai().value(QStringLiteral("shareNotes")).toBool()) {
        for (const Note &n : dayNotes(q, from, to, z)) {
            if (!n.content.trimmed().isEmpty())
                context.dayNotes << QStringLiteral("%1: %2").arg(n.title, n.content.simplified().left(500));
        }
    }
    context.userHints = q.setting(QStringLiteral("ai_scheduling_hints"), QString()).toString();

    const ai::Request request = ai::weekRequest(context);
    setAiBusy(true);
    ai::runValidated(p, request, [this, request](const ai::Result &r) {
        setAiBusy(false);
        logAi(request.feature, r, request.prompt);
        if (!r.ok) {
            Q_EMIT aiFailed(request.feature, r.error);
            return;
        }
        const ai::WeekSummary s = ai::toWeekSummary(r.data);
        Q_EMIT weekSummaryReady(QVariantMap{{QStringLiteral("summary"), s.summary},
                                            {QStringLiteral("suggestions"), s.suggestions},
                                            {QStringLiteral("by"), ai().value(QStringLiteral("label"))}});
    });
}

void AppController::finishReview(bool quietly)
{
    applyExternal(ops::setSetting(QStringLiteral("review_last"), localDate(now(), zone()).toString(Qt::ISODate)));
    if (!quietly)
        Q_EMIT message(QStringLiteral("Week reviewed. Nice work looking back."));
}

} // namespace tumbler
