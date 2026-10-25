#pragma once

#include "entities.h"
#include "mutation.h"
#include "queries.h"

#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

// Pulling tasks and events in from other services, as plain rules over the
// database so they can be tested without a network. The service clients
// (src/sync) fetch; these functions decide what changes.
//
// Rules, carried over from the web app with two fixes:
// - The service is the source of truth for a linked task's title, notes,
//   priority and due date, but only when the remote item changed: local
//   edits stand until then.
// - Something that disappears remotely is closed here (completed if it was
//   completed there, cancelled otherwise) and unlinked.
// - New: deleting a linked task here keeps the link with no task, so the
//   next pull doesn't bring it back.
// - New: finishing a linked task here is pushed once (done_pushed), and
//   can be retried until the service accepts it.
namespace tumbler::sync {

struct RemoteTask {
    QString externalId;
    QString title;
    QString description;
    Priority priority = Priority::Medium;
    QDate dueDate;
    QStringList tags;
    QString url; // becomes an "open" launcher on the task
    bool open = true;

    QString hash() const;
};

// What happened to an item that no longer matches the pull.
enum class Gone {
    Completed, // done there
    Cancelled, // cancelled or deleted there
    Elsewhere, // still open, but no longer yours (reassigned, moved out of the synced teams/lists)
    Unknown,   // couldn't find out (network); try again next time
};

struct PullResult {
    int created = 0;
    int updated = 0;
    int closed = 0;
    int unchanged = 0;
    QStringList notes; // one line per closed task, for the sync summary
};

// The integration row for a provider, created if missing. Not undoable.
std::optional<QString> ensureIntegration(QSqlDatabase db, const QString &provider, qint64 now, QString *error = nullptr);

// Linked items not in `active`: the caller asks the service what became of them.
QStringList missing(const Queries &q, const QString &integrationId, const QList<RemoteTask> &active);

// The whole pull as one mutation. `gone` answers missing() (absent = Unknown).
std::optional<Mutation> pullTasks(const Queries &q, const QString &integrationId, const QList<RemoteTask> &active,
                                  const QHash<QString, Gone> &gone, qint64 now, PullResult *result,
                                  QString *error = nullptr);

// Linked tasks finished here that the service hasn't been told about.
struct PendingPush {
    QString externalId;
    QString taskId;
    QString title;
};
QList<PendingPush> pendingCompletions(const Queries &q, const QString &integrationId);
Mutation markPushed(const QString &integrationId, const QString &externalId);

// ---- calendars ---------------------------------------------------------------

struct RemoteEvent {
    QString externalId; // unique per occurrence: UID plus the recurrence instant
    Event event;        // id ignored; blocking and colour come from the feed's settings
    QString hash() const;
};

struct FeedResult {
    int created = 0;
    int updated = 0;
    int removed = 0;
};

// Replaces what a feed contributed between `from` and `to` (UTC ms) with
// `events`. Events before the window stay as history.
std::optional<Mutation> pullFeed(const Queries &q, const QString &integrationId, const QString &feedId,
                                 const QList<RemoteEvent> &events, qint64 from, qint64 to, const QString &timezone,
                                 qint64 now, FeedResult *result, QString *error = nullptr);

// Everything a feed brought in, for when it's removed.
Mutation dropFeed(const QString &integrationId, const QString &feedId);

// Which feed an event came from, if any (such events are read-only here).
QString feedOf(const Queries &q, const QString &eventId);

} // namespace tumbler::sync
