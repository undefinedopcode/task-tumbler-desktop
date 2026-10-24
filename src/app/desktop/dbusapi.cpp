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

} // namespace tumbler
