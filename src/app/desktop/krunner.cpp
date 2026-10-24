#include "krunner.h"

#include "appcontroller.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace tumbler {

QDBusArgument &operator<<(QDBusArgument &arg, const RemoteMatch &m)
{
    arg.beginStructure();
    arg << m.id << m.text << m.iconName << m.type << m.relevance << m.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, RemoteMatch &m)
{
    arg.beginStructure();
    arg >> m.id >> m.text >> m.iconName >> m.type >> m.relevance >> m.properties;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const RemoteAction &a)
{
    arg.beginStructure();
    arg << a.id << a.text << a.iconName;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, RemoteAction &a)
{
    arg.beginStructure();
    arg >> a.id >> a.text >> a.iconName;
    arg.endStructure();
    return arg;
}

KRunnerApi::KRunnerApi(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();
    qDBusRegisterMetaType<RemoteAction>();
    qDBusRegisterMetaType<RemoteActions>();
}

bool KRunnerApi::registerOnBus()
{
    return QDBusConnection::sessionBus().registerObject(QStringLiteral("/runner"), this, QDBusConnection::ExportAllSlots);
}

bool KRunnerApi::setPluginInstalled(bool installed)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/krunner/dbusplugins");
    const QString path = dir + QStringLiteral("/org.kde.tasktumbler.desktop");
    if (!installed)
        return !QFile::exists(path) || QFile::remove(path);
    QDir().mkpath(dir);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write("[Desktop Entry]\n"
            "Name=Task Tumbler\n"
            "Comment=Capture tasks with \"tt ...\", or find a task to start or finish\n"
            "Icon=org.kde.tasktumbler\n"
            "X-KDE-ServiceTypes=Plasma/Runner\n"
            "Type=Service\n"
            "X-KDE-PluginInfo-Name=org.kde.tasktumbler\n"
            "X-KDE-PluginInfo-EnabledByDefault=true\n"
            "X-Plasma-API=DBus\n"
            "X-Plasma-DBusRunner-Service=org.kde.tasktumbler\n"
            "X-Plasma-DBusRunner-Path=/runner\n");
    return true;
}

RemoteActions KRunnerApi::Actions()
{
    return {
        {QStringLiteral("start"), QStringLiteral("Start working on it"), QStringLiteral("media-playback-start")},
        {QStringLiteral("done"), QStringLiteral("Mark done"), QStringLiteral("checkmark")},
    };
}

RemoteMatches KRunnerApi::Match(const QString &query)
{
    RemoteMatches out;
    for (const QVariant &v : m_app->runnerMatches(query)) {
        const QVariantMap m = v.toMap();
        RemoteMatch r;
        r.id = m.value(QStringLiteral("id")).toString();
        r.text = m.value(QStringLiteral("text")).toString();
        r.iconName = m.value(QStringLiteral("icon")).toString();
        r.type = m.value(QStringLiteral("exact")).toBool() ? 100 : 50;
        r.relevance = m.value(QStringLiteral("relevance")).toDouble();
        r.properties.insert(QStringLiteral("subtext"), m.value(QStringLiteral("subtext")));
        // Start/Done only make sense for existing tasks.
        if (r.id.startsWith(QLatin1String("task:")))
            r.properties.insert(QStringLiteral("actions"), QStringList{QStringLiteral("start"), QStringLiteral("done")});
        else
            r.properties.insert(QStringLiteral("actions"), QStringList());
        out.append(r);
    }
    return out;
}

void KRunnerApi::Run(const QString &matchId, const QString &actionId)
{
    m_app->runRunnerMatch(matchId, actionId);
}

} // namespace tumbler
