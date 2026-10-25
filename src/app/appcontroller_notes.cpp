// AppController: notes. Markdown text, searchable, optionally about a task.

#include "appcontroller.h"
#include "notes.h"
#include "operations.h"

#include <QFile>
#include <QUrl>

namespace tumbler {

namespace {

QVariantMap toVariant(const Note &n)
{
    return {
        {QStringLiteral("id"), n.id},
        {QStringLiteral("title"), n.title},
        {QStringLiteral("content"), n.content},
        {QStringLiteral("taskId"), n.taskId},
        {QStringLiteral("tags"), n.tags},
        {QStringLiteral("color"), n.color},
        {QStringLiteral("createdAt"), n.createdAt},
        {QStringLiteral("updatedAt"), n.updatedAt},
    };
}

// Matches arrive wrapped in \x02…\x03; everything else is the user's text.
QString snippetHtml(const QString &snippet)
{
    QString html = snippet.toHtmlEscaped();
    html.replace(QChar(0x02), QStringLiteral("<b>"));
    html.replace(QChar(0x03), QStringLiteral("</b>"));
    return html;
}

void setFields(Note &n, const QVariantMap &f)
{
    if (f.contains(QStringLiteral("title")))
        n.title = f.value(QStringLiteral("title")).toString();
    if (f.contains(QStringLiteral("content")))
        n.content = f.value(QStringLiteral("content")).toString();
    if (f.contains(QStringLiteral("taskId")))
        n.taskId = f.value(QStringLiteral("taskId")).toString();
    if (f.contains(QStringLiteral("tags")))
        n.tags = f.value(QStringLiteral("tags")).toStringList();
    if (f.contains(QStringLiteral("color")))
        n.color = f.value(QStringLiteral("color")).toString();
}

} // namespace

QVariantList AppController::notes(const QString &search, const QString &tag, const QString &taskId) const
{
    QVariantList out;
    for (const NoteHit &h : notes::find(queries(), search, tag, taskId)) {
        QVariantMap m = toVariant(h.note);
        m.remove(QStringLiteral("content"));
        m.insert(QStringLiteral("snippet"), snippetHtml(h.snippet));
        m.insert(QStringLiteral("taskTitle"), h.taskTitle);
        out.append(m);
    }
    return out;
}

QVariantList AppController::noteTaskChoices() const
{
    QList<Task> open;
    for (const Task &t : queries().tasks()) {
        if (t.status == TaskStatus::Pending || t.status == TaskStatus::InProgress)
            open.append(t);
    }
    std::sort(open.begin(), open.end(), [](const Task &a, const Task &b) { return a.title.localeAwareCompare(b.title) < 0; });
    QVariantList out;
    for (const Task &t : open)
        out.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("title"), t.title}});
    return out;
}

QStringList AppController::noteTags() const
{
    return notes::tags(queries());
}

QVariantMap AppController::noteDetail(const QString &id) const
{
    const auto n = notes::get(queries(), id);
    if (!n)
        return {};
    QVariantMap m = toVariant(*n);
    const auto task = n->taskId.isEmpty() ? std::nullopt : queries().task(n->taskId);
    m.insert(QStringLiteral("taskTitle"), task ? task->title : QString());
    return m;
}

QString AppController::createNote(const QVariantMap &fields)
{
    Note n;
    n.id = newId();
    setFields(n, fields);
    if (n.title.trimmed().isEmpty())
        n.title = QStringLiteral("Untitled note");
    return apply(notes::save(n, true, now())) ? n.id : QString();
}

bool AppController::saveNote(const QString &id, const QVariantMap &fields)
{
    auto n = notes::get(queries(), id);
    if (!n)
        return false;
    setFields(*n, fields);
    QString error;
    if (!applyExternal(notes::save(*n, false, now()), &error)) {
        Q_EMIT message(QStringLiteral("Couldn't save the note: %1").arg(error));
        return false;
    }
    return true;
}

bool AppController::appendToNote(const QString &id, const QString &text)
{
    auto n = notes::get(queries(), id);
    if (!n || text.trimmed().isEmpty())
        return false;
    n->content = n->content.trimmed().isEmpty() ? text.trimmed() : n->content.trimmed() + QStringLiteral("\n\n") + text.trimmed();
    Mutation m = notes::save(*n, false, now());
    m.text = QStringLiteral("Add to note “%1”").arg(n->title);
    return apply(m);
}

bool AppController::deleteNote(const QString &id)
{
    const auto n = notes::get(queries(), id);
    if (!n)
        return false;
    Mutation m = notes::remove(id);
    m.text = QStringLiteral("Delete note “%1”").arg(n->title);
    if (!apply(m))
        return false;
    Q_EMIT message(QStringLiteral("Deleted “%1” (Ctrl+Z to undo)").arg(n->title));
    return true;
}

bool AppController::exportNote(const QString &id, const QUrl &file)
{
    const auto n = notes::get(queries(), id);
    if (!n)
        return false;
    QFile f(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT message(QStringLiteral("Couldn't write %1").arg(f.fileName()));
        return false;
    }
    QString text = QStringLiteral("# ") + n->title + QStringLiteral("\n\n");
    if (!n->tags.isEmpty())
        text += QStringLiteral("Tags: ") + n->tags.join(QStringLiteral(", ")) + QStringLiteral("\n\n");
    f.write((text + n->content).toUtf8());
    Q_EMIT message(QStringLiteral("Saved %1").arg(f.fileName()));
    return true;
}

} // namespace tumbler
