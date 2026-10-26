#include "presence.h"

#include <KIdleTime>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace tumbler::presence {

qint64 idleMs()
{
    return KIdleTime::instance()->idleTime();
}

bool away(int minutes)
{
    if (idleMs() >= minutes * 60000LL)
        return true;
    QDBusInterface saver(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                         QStringLiteral("org.freedesktop.ScreenSaver"), QDBusConnection::sessionBus());
    const QDBusReply<bool> locked = saver.call(QStringLiteral("GetActive"));
    return locked.isValid() && locked.value();
}

} // namespace tumbler::presence
