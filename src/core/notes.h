#pragma once

#include "mutation.h"
#include "queries.h"

#include <QString>
#include <QStringList>
#include <optional>

namespace tumbler {

// A note: markdown text, optionally about a task. Searched with FTS5.
struct Note {
    QString id;
    QString title;
    QString content;
    QString taskId;
    QStringList tags;
    QString color;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
};

struct NoteHit {
    Note note;
    // Around the best match when searching (matches wrapped in \x02…\x03),
    // otherwise the start of the note.
    QString snippet;
    QString taskTitle;
};

namespace notes {

// What the user typed, as an FTS5 query: every word must appear, each as a
// prefix ("meet" finds "meeting"). Quotes and operators are taken literally.
QString ftsQuery(const QString &text);

// Newest first, or best match first when searching. Empty filters match all.
QList<NoteHit> find(const Queries &q, const QString &search = {}, const QString &tag = {}, const QString &taskId = {});
std::optional<Note> get(const Queries &q, const QString &id);
// Every tag in use, most used first.
QStringList tags(const Queries &q);

Mutation save(const Note &note, bool isNew, qint64 now);
Mutation remove(const QString &id);

} // namespace notes
} // namespace tumbler
