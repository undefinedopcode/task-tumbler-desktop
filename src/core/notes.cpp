#include "notes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

namespace tumbler::notes {

namespace {

const char kColumns[] = "n.id, n.title, n.content, n.task_id, n.tags, n.color, n.created_at, n.updated_at";

Note read(const QSqlQuery &q)
{
    Note n;
    n.id = q.value(0).toString();
    n.title = q.value(1).toString();
    n.content = q.value(2).toString();
    n.taskId = q.value(3).toString();
    for (const QJsonValue &t : QJsonDocument::fromJson(q.value(4).toString().toUtf8()).array())
        n.tags.append(t.toString());
    n.color = q.value(5).toString();
    n.createdAt = q.value(6).toLongLong();
    n.updatedAt = q.value(7).toLongLong();
    return n;
}

QString opening(const QString &content)
{
    // The first few lines, without markdown punctuation getting in the way.
    QString s = content.left(400);
    s.replace(QRegularExpression(QStringLiteral("^[#>*\\-\\s]+|\\[([^\\]]*)\\]\\([^)]*\\)"), QRegularExpression::MultilineOption),
              QStringLiteral("\\1"));
    s.remove(QRegularExpression(QStringLiteral("[*_`~]{1,3}(?=\\S)|(?<=\\S)[*_`~]{1,3}"))); // emphasis markers
    s = s.simplified();
    return s.size() > 160 ? s.left(159) + QChar(0x2026) : s;
}

bool exec(QSqlDatabase db, const QString &sql, const QVariantList &binds, QString *error)
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}

QString tagsJson(const QStringList &tags)
{
    QJsonArray a;
    QStringList seen;
    for (const QString &t : tags) {
        const QString tag = t.trimmed().remove(QRegularExpression(QStringLiteral("^#+")));
        if (!tag.isEmpty() && !seen.contains(tag, Qt::CaseInsensitive)) {
            seen.append(tag);
            a.append(tag);
        }
    }
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

} // namespace

QString ftsQuery(const QString &text)
{
    QStringList terms;
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}_]+"));
    for (auto it = word.globalMatch(text); it.hasNext();)
        terms.append(QLatin1Char('"') + it.next().captured() + QStringLiteral("\"*"));
    return terms.join(QLatin1Char(' '));
}

QList<NoteHit> find(const Queries &q, const QString &search, const QString &tag, const QString &taskId)
{
    const QString match = ftsQuery(search);
    QString sql = QStringLiteral("SELECT %1, %2, t.title FROM %3 LEFT JOIN tasks t ON t.id = n.task_id WHERE 1")
                      .arg(QLatin1String(kColumns),
                           match.isEmpty() ? QStringLiteral("''") : QStringLiteral("snippet(notes_fts, -1, char(2), char(3), '…', 16)"),
                           match.isEmpty() ? QStringLiteral("notes n") : QStringLiteral("notes_fts JOIN notes n ON n.rowid = notes_fts.rowid"));
    QVariantList binds;
    if (!match.isEmpty()) {
        sql += QStringLiteral(" AND notes_fts MATCH ?");
        binds.append(match);
    }
    if (!tag.isEmpty()) {
        sql += QStringLiteral(" AND EXISTS (SELECT 1 FROM json_each(n.tags) WHERE lower(value) = lower(?))");
        binds.append(tag);
    }
    if (!taskId.isEmpty()) {
        sql += QStringLiteral(" AND n.task_id = ?");
        binds.append(taskId);
    }
    sql += match.isEmpty() ? QStringLiteral(" ORDER BY n.updated_at DESC") : QStringLiteral(" ORDER BY bm25(notes_fts, 10.0, 1.0), n.updated_at DESC") /* a title match counts for more */;

    QSqlQuery s(q.db());
    s.prepare(sql);
    for (const QVariant &b : binds)
        s.addBindValue(b);
    QList<NoteHit> out;
    if (!s.exec())
        return out;
    while (s.next()) {
        NoteHit h;
        h.note = read(s);
        h.snippet = match.isEmpty() ? opening(h.note.content) : s.value(8).toString().simplified();
        h.taskTitle = s.value(9).toString();
        out.append(h);
    }
    return out;
}

std::optional<Note> get(const Queries &q, const QString &id)
{
    QSqlQuery s(q.db());
    s.prepare(QStringLiteral("SELECT %1 FROM notes n WHERE n.id = ?").arg(QLatin1String(kColumns)));
    s.addBindValue(id);
    if (!s.exec() || !s.next())
        return std::nullopt;
    return read(s);
}

QStringList tags(const Queries &q)
{
    QStringList out;
    QSqlQuery s(q.db());
    if (!s.exec(QStringLiteral("SELECT value, COUNT(*) AS n FROM notes, json_each(notes.tags) GROUP BY lower(value) ORDER BY n DESC, lower(value)")))
        return out;
    while (s.next())
        out.append(s.value(0).toString());
    return out;
}

Mutation save(const Note &n, bool isNew, qint64 now)
{
    Mutation m;
    const QString title = n.title.trimmed().isEmpty() ? QStringLiteral("Untitled note") : n.title.trimmed();
    m.text = isNew ? QStringLiteral("Add note “%1”").arg(title) : QStringLiteral("Edit note “%1”").arg(title);
    m.scopes = {{QStringLiteral("notes"), QStringLiteral("id = ?"), {n.id}}};
    // A null QString would bind as NULL; content is NOT NULL.
    const QVariantList values{title, n.content.isNull() ? QString(QLatin1String("")) : n.content, n.taskId.isEmpty() ? QVariant() : QVariant(n.taskId), tagsJson(n.tags),
                              n.color.isEmpty() ? QVariant() : QVariant(n.color)};
    const QString id = n.id;
    m.apply = [id, values, isNew, now](QSqlDatabase db, QString *error) {
        QVariantList binds = values;
        if (isNew) {
            binds.prepend(id);
            binds << now << now;
            return exec(db, QStringLiteral("INSERT INTO notes (id, title, content, task_id, tags, color, created_at, updated_at) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
                        binds, error);
        }
        binds << now << id;
        return exec(db, QStringLiteral("UPDATE notes SET title = ?, content = ?, task_id = ?, tags = ?, color = ?, updated_at = ? WHERE id = ?"),
                    binds, error);
    };
    return m;
}

Mutation remove(const QString &id)
{
    Mutation m;
    m.text = QStringLiteral("Delete note");
    m.scopes = {{QStringLiteral("notes"), QStringLiteral("id = ?"), {id}}};
    m.apply = [id](QSqlDatabase db, QString *error) { return exec(db, QStringLiteral("DELETE FROM notes WHERE id = ?"), {id}, error); };
    return m;
}

} // namespace tumbler::notes
