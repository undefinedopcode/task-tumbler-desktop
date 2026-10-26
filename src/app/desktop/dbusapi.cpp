#include "dbusapi.h"

#include "appcontroller.h"

#include <QDBusConnection>
#include <QJsonDocument>
#include <QJsonValue>

namespace tumbler {

namespace {
QString json(const QVariant &v)
{
    return QString::fromUtf8(QJsonDocument::fromVariant(v).toJson(QJsonDocument::Compact));
}
} // namespace

DBusApi::DBusApi(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

bool DBusApi::registerOnBus()
{
    return QDBusConnection::sessionBus().registerObject(QStringLiteral("/Tasks"), this, QDBusConnection::ExportScriptableSlots);
}

bool DBusApi::mayWrite()
{
    if (m_app->externalWritesAllowed())
        return true;
    if (calledFromDBus())
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Task Tumbler is set to not let other apps change tasks"));
    return false;
}

QString DBusApi::Today()
{
    return json(m_app->today());
}

QString DBusApi::Tasks(const QString &filter, const QString &search)
{
    return json(m_app->externalTasks(filter.isEmpty() ? QStringLiteral("open") : filter, search));
}

QString DBusApi::Task(const QString &id)
{
    QVariantMap t = m_app->taskDetail(id);
    if (t.isEmpty() && calledFromDBus())
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("no task with id %1").arg(id));
    t.remove(QStringLiteral("actions")); // launchers stay private to the app
    return json(t);
}

QString DBusApi::Current()
{
    return json(m_app->working());
}

QString DBusApi::Capture(const QString &text)
{
    if (!mayWrite())
        return {};
    return m_app->capture(text);
}

bool DBusApi::Complete(const QString &id)
{
    return mayWrite() && m_app->setTaskStatus(id, QStringLiteral("completed"));
}

bool DBusApi::StartWork(const QString &id)
{
    return mayWrite() && m_app->startWork(id);
}

bool DBusApi::StopWork()
{
    return mayWrite() && m_app->stopWork();
}

bool DBusApi::ScheduleNextSlot(const QString &id)
{
    return mayWrite() && m_app->scheduleNextSlot(id);
}

void DBusApi::Show()
{
    Q_EMIT m_app->showWindowRequested();
}

QString DBusApi::Notes(const QString &search, const QString &tag)
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

QString DBusApi::Stats(int days)
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

QString DBusApi::Note(const QString &id)
{
    QVariantMap n = m_app->noteDetail(id);
    if (n.isEmpty() && calledFromDBus())
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("no note with id %1").arg(id));
    n.remove(QStringLiteral("color"));
    return json(n);
}

QString DBusApi::AddNote(const QString &title, const QString &content, const QString &taskId, const QString &tags)
{
    if (!mayWrite())
        return {};
    if (!taskId.isEmpty() && m_app->taskDetail(taskId).isEmpty()) {
        if (calledFromDBus())
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("no task with id %1").arg(taskId));
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

bool DBusApi::AppendToNote(const QString &id, const QString &text)
{
    return mayWrite() && m_app->appendToNote(id, text);
}

} // namespace tumbler
