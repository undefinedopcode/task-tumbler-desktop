#include "focusguard.h"

#include "appcontroller.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace tumbler {

namespace {
QDBusInterface notifications()
{
    return QDBusInterface(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                          QStringLiteral("org.freedesktop.Notifications"), QDBusConnection::sessionBus());
}
} // namespace

FocusGuard::FocusGuard(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    connect(app, &AppController::focusModeChanged, this, &FocusGuard::update);
}

FocusGuard::~FocusGuard()
{
    if (m_cookie)
        notifications().call(QStringLiteral("UnInhibit"), m_cookie);
}

void FocusGuard::update()
{
    if (m_app->focusMode() && !m_cookie) {
        const QString reason = QStringLiteral("Focusing on %1").arg(m_app->working().value(QStringLiteral("title")).toString());
        const QDBusReply<uint> reply = notifications().call(QStringLiteral("Inhibit"), QStringLiteral("org.kde.tasktumbler"), reason, QVariantMap());
        if (reply.isValid())
            m_cookie = reply.value();
    } else if (!m_app->focusMode() && m_cookie) {
        notifications().call(QStringLiteral("UnInhibit"), m_cookie);
        m_cookie = 0;
    }
}

} // namespace tumbler
