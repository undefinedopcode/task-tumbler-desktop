#include "dbusbackend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDeadlineTimer>
#include <QProcess>
#include <QThread>

namespace tumbler::mcp {

namespace {
const QString kService = QStringLiteral("org.kde.tasktumbler");
}

bool DBusBackend::ensureRunning(QString *error)
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        *error = QStringLiteral("no D-Bus session bus");
        return false;
    }
    if (bus->isServiceRegistered(kService))
        return true;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--hidden")})) {
        *error = QStringLiteral("Task Tumbler isn't running and couldn't be started");
        return false;
    }
    QDeadlineTimer deadline(15000);
    while (!deadline.hasExpired()) {
        if (bus->isServiceRegistered(kService))
            return true;
        QThread::msleep(200);
    }
    *error = QStringLiteral("Task Tumbler didn't start in time");
    return false;
}

QVariant DBusBackend::call(const QString &method, const QVariantList &args, QString *error)
{
    if (!ensureRunning(error))
        return {};
    QDBusInterface api(kService, QStringLiteral("/Tasks"), QStringLiteral("org.kde.tasktumbler.Tasks"), QDBusConnection::sessionBus());
    const QDBusMessage reply = api.callWithArgumentList(QDBus::Block, method, args);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = reply.errorMessage();
        return {};
    }
    return reply.arguments().value(0);
}

} // namespace tumbler::mcp
