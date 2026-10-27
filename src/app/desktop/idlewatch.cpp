#include "idlewatch.h"

#include "appcontroller.h"

#include <KLocalizedString>
#include <KNotification>
#include <QDateTime>
#include <QPointer>
#include <algorithm>

namespace tumbler {

IdleWatch::IdleWatch(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    startNoticing();
    connect(app, &AppController::settingsChanged, this, &IdleWatch::configure);
    configure();
}

void IdleWatch::wentAway(qint64 since)
{
    if (m_since == 0 && !m_app->working().isEmpty())
        m_since = since;
}

void IdleWatch::cameBack(qint64 at)
{
    if (m_since == 0)
        return;
    m_app->reportAway(m_since, at);
    m_since = 0;
    if (!m_app->away().isEmpty())
        notifyReturn();
}

void IdleWatch::configure()
{
    const int minutes = m_app->settings().value(QStringLiteral("idleMinutes"), 10).toInt();
    if (minutes == m_minutes)
        return;
    m_minutes = minutes;
    watchFor(std::max(0, minutes));
}

void IdleWatch::notifyReturn()
{
    const QVariantMap away = m_app->away();
    auto *n = new KNotification(QStringLiteral("wrapUp"), KNotification::Persistent);
    n->setComponentName(QStringLiteral("tasktumbler"));
    n->setIconName(QStringLiteral("org.kde.tasktumbler"));
    n->setTitle(i18n("Welcome back"));
    n->setText(i18n("You were away for %1 min. Still working on “%2”?", away.value(QStringLiteral("minutes")).toInt(),
                    away.value(QStringLiteral("title")).toString()));
    QPointer<AppController> app = m_app;
    const auto choose = [app](const QString &choice) {
        return [app, choice] {
            if (app)
                app->resolveAway(choice);
        };
    };
    connect(n->addAction(i18n("Yes, keep the time")), &KNotificationAction::activated, this, choose(QStringLiteral("keep")));
    connect(n->addAction(i18n("Leave the break out")), &KNotificationAction::activated, this, choose(QStringLiteral("trimRestart")));
    connect(n->addAction(i18n("Stop at %1", away.value(QStringLiteral("sinceLabel")).toString())), &KNotificationAction::activated, this,
            choose(QStringLiteral("trim")));
    // Answered in the app instead: close the notification.
    connect(m_app, &AppController::awayChanged, n, [n, app] {
        if (app && app->away().isEmpty())
            n->close();
    });
    n->sendEvent();
}

} // namespace tumbler
