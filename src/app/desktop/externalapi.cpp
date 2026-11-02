#include "externalapi.h"

#include "appcontroller.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <utility>
#ifdef TUMBLER_HAVE_DBUS
#include <QDBusConnection>
#endif

namespace tumbler {

namespace {
QString json(const QVariant &v)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact));
}
} // namespace

ExternalApi::ExternalApi(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

#ifdef TUMBLER_HAVE_DBUS
bool ExternalApi::registerOnBus()
{
    return QDBusConnection::sessionBus().registerObject(QStringLiteral("/Tasks"), this, QDBusConnection::ExportScriptableSlots);
}
#endif

QString ExternalApi::takeError()
{
    return std::exchange(m_error, QString());
}

void ExternalApi::fail(Failure kind, const QString &message)
{
    m_error = message;
#ifdef TUMBLER_HAVE_DBUS
    if (calledFromDBus())
        sendErrorReply(kind == Failure::Denied ? QDBusError::AccessDenied : QDBusError::InvalidArgs, message);
#else
    Q_UNUSED(kind)
#endif
}

bool ExternalApi::mayWrite()
{
    if (m_app->externalWritesAllowed())
        return true;
    fail(Failure::Denied, QStringLiteral("Task Tumbler is set to not let other apps change tasks"));
    return false;
}

QString ExternalApi::Today()
{
    return json(m_app->today());
}

QString ExternalApi::Tasks(const QString &filter, const QString &search)
{
    return json(m_app->externalTasks(filter.isEmpty() ? QStringLiteral("open") : filter, search));
}

QString ExternalApi::Task(const QString &id)
{
    QVariantMap t = m_app->taskDetail(id);
    if (t.isEmpty())
        fail(Failure::BadArgument, QStringLiteral("no task with id %1").arg(id));
    t.remove(QStringLiteral("actions")); // launchers stay private to the app
    return json(t);
}

QString ExternalApi::Current()
{
    return json(m_app->working());
}

QString ExternalApi::Capture(const QString &text)
{
    if (!mayWrite())
        return {};
    return m_app->capture(text);
}

bool ExternalApi::Complete(const QString &id)
{
    return mayWrite() && m_app->setTaskStatus(id, QStringLiteral("completed"));
}

bool ExternalApi::StartWork(const QString &id)
{
    return mayWrite() && m_app->startWork(id);
}

bool ExternalApi::StopWork()
{
    return mayWrite() && m_app->stopWork();
}

bool ExternalApi::ScheduleNextSlot(const QString &id)
{
    return mayWrite() && m_app->scheduleNextSlot(id);
}

void ExternalApi::Show()
{
    Q_EMIT m_app->showWindowRequested();
}

QString ExternalApi::Notes(const QString &search, const QString &tag)
{
    QVariantList out;
    for (const QVariant &v : m_app->notes(search, tag)) {
        QVariantMap n = v.toMap();
        // The UI's snippet is rich text; callers get it plain.
        QString s = n.value(QStringLiteral("snippet")).toString();
        s.remove(QStringLiteral("<b>")).remove(QStringLiteral("</b>"));
        s.replace(QStringLiteral("&lt;"), QStringLiteral("<")).replace(QStringLiteral("&gt;"), QStringLiteral(">"))
            .replace(QStringLiteral("&quot;"), QStringLiteral("\"")).replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        n.insert(QStringLiteral("snippet"), s);
        n.remove(QStringLiteral("color"));
        out.append(n);
    }
    return json(out);
}

QString ExternalApi::Stats(int days)
{
    QVariantMap s = m_app->stats(days);
    // Only the hours with something in them.
    QVariantList hours;
    for (const QVariant &v : s.value(QStringLiteral("hours")).toList()) {
        const QVariantMap h = v.toMap();
        if (h.value(QStringLiteral("minutes")).toInt() > 0 || !h.value(QStringLiteral("energy")).isNull())
            hours.append(h);
    }
    s.insert(QStringLiteral("hours"), hours);
    return json(s);
}

QString ExternalApi::Note(const QString &id)
{
    QVariantMap n = m_app->noteDetail(id);
    if (n.isEmpty())
        fail(Failure::BadArgument, QStringLiteral("no note with id %1").arg(id));
    n.remove(QStringLiteral("color"));
    return json(n);
}

QString ExternalApi::AddNote(const QString &title, const QString &content, const QString &taskId, const QString &tags)
{
    if (!mayWrite())
        return {};
    if (!taskId.isEmpty() && m_app->taskDetail(taskId).isEmpty()) {
        fail(Failure::BadArgument, QStringLiteral("no task with id %1").arg(taskId));
        return {};
    }
    QStringList tagList;
    for (const QString &t : tags.split(QLatin1Char(','), Qt::SkipEmptyParts))
        tagList.append(t.trimmed());
    return m_app->createNote({{QStringLiteral("title"), title},
                              {QStringLiteral("content"), content},
                              {QStringLiteral("taskId"), taskId},
                              {QStringLiteral("tags"), tagList}});
}

bool ExternalApi::AppendToNote(const QString &id, const QString &text)
{
    return mayWrite() && m_app->appendToNote(id, text);
}

bool ExternalApi::EditNote(const QString &id, const QString &oldText, const QString &newText)
{
    if (!mayWrite())
        return false;
    const QVariantMap n = m_app->noteDetail(id);
    if (n.isEmpty()) {
        fail(Failure::BadArgument, QStringLiteral("no note with id %1").arg(id));
        return false;
    }
    const qsizetype found = oldText.isEmpty() ? 0 : n.value(QStringLiteral("content")).toString().count(oldText);
    if (found != 1) {
        fail(Failure::BadArgument, found == 0 ? QStringLiteral("old_text isn't in the note; copy it exactly from get_note")
                                              : QStringLiteral("old_text appears %1 times; include more around it so it's unique").arg(found));
        return false;
    }
    if (oldText == newText) {
        fail(Failure::BadArgument, QStringLiteral("new_text is the same as old_text"));
        return false;
    }
    return m_app->replaceInNote(id, oldText, newText);
}

QString ExternalApi::CloseOut()
{
    return json(m_app->closeOut());
}

bool ExternalApi::ResolveLeftover(const QString &kind, const QString &id, const QString &choice)
{
    return mayWrite() && m_app->resolveLeftover(kind, id, choice);
}

bool ExternalApi::ClearLeftovers()
{
    return mayWrite() && m_app->clearLeftovers();
}

QString ExternalApi::AddToDayNote(const QString &text)
{
    if (!mayWrite() || text.trimmed().isEmpty())
        return {};
    // Added to, never replaced: the note is the user's own words. Undoable,
    // unlike the close-out page's note box, which saves as you type.
    const QVariantMap day = m_app->closeOut();
    const QString id = day.value(QStringLiteral("noteId")).toString();
    if (!id.isEmpty())
        return m_app->appendToNote(id, text) ? id : QString();
    return m_app->createNote({{QStringLiteral("title"), day.value(QStringLiteral("dateLabel"))},
                              {QStringLiteral("content"), text},
                              {QStringLiteral("tags"), QStringList{QStringLiteral("day")}}});
}

bool ExternalApi::FinishCloseOut()
{
    if (!mayWrite())
        return false;
    m_app->finishCloseOut();
    return true;
}

bool ExternalApi::LetGo(const QString &id)
{
    return mayWrite() && m_app->letGo(id);
}

QString ExternalApi::WeekReview()
{
    return json(m_app->weekReview());
}

bool ExternalApi::FinishReview()
{
    if (!mayWrite())
        return false;
    m_app->finishReview();
    return true;
}

} // namespace tumbler
