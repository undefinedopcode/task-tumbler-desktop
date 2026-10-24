#include "tray.h"

#include "appcontroller.h"

#include <KLocalizedString>
#include <KStatusNotifierItem>
#include <QAction>
#include <QDateTime>
#include <QIcon>
#include <QLocale>
#include <QMenu>
#include <QWindow>

namespace tumbler {

Tray::Tray(AppController *app, QWindow *window, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_item(new KStatusNotifierItem(QStringLiteral("org.kde.tasktumbler"), this))
{
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setTitle(i18n("Task Tumbler"));
    const QIcon icon = QIcon::fromTheme(QStringLiteral("org.kde.tasktumbler"), QIcon(QStringLiteral(":/icons/org.kde.tasktumbler.svg")));
    m_item->setIconByPixmap(icon);
    m_item->setAssociatedWindow(window);
    m_item->setStandardActionsEnabled(true);

    QMenu *menu = m_item->contextMenu();
    auto *capture = new QAction(QIcon::fromTheme(QStringLiteral("view-task-add")), i18n("Quick Capture…"), menu);
    connect(capture, &QAction::triggered, app, &AppController::showCaptureRequested);
    m_timerAction = new QAction(menu);
    connect(m_timerAction, &QAction::triggered, this, [this] {
        if (!m_app->working().isEmpty())
            m_app->stopWork();
        else
            Q_EMIT m_app->showWindowRequested();
    });
    menu->addAction(capture);
    menu->addAction(m_timerAction);
    menu->addSeparator();

    connect(app, &AppController::workingChanged, this, &Tray::update);
    connect(app, &AppController::todayChanged, this, &Tray::update);
    // Keeps the elapsed minutes in the tooltip current.
    m_refresh.setInterval(60 * 1000);
    connect(&m_refresh, &QTimer::timeout, this, &Tray::update);
    m_refresh.start();
    update();
}

void Tray::update()
{
    const QVariantMap working = m_app->working();
    QString subtitle;
    if (!working.isEmpty()) {
        const qint64 minutes = (m_app->now() - working.value(QStringLiteral("startedAt")).toLongLong()) / 60000;
        subtitle = i18n("Working on %1 for %2 min", working.value(QStringLiteral("title")).toString(), minutes);
        m_item->setOverlayIconByName(QStringLiteral("chronometer-start"));
        m_timerAction->setText(i18n("Stop Working on “%1”", working.value(QStringLiteral("title")).toString()));
        m_timerAction->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-stop")));
    } else {
        m_item->setOverlayIconByName(QString());
        m_timerAction->setText(i18n("Show Today"));
        m_timerAction->setIcon(QIcon::fromTheme(QStringLiteral("view-calendar-day")));
        const QVariantMap today = m_app->today();
        const QVariantList now = today.value(QStringLiteral("now")).toList();
        const QVariantList upcoming = today.value(QStringLiteral("upcoming")).toList();
        if (!now.isEmpty()) {
            subtitle = i18n("Now: %1", now.first().toMap().value(QStringLiteral("title")).toString());
        } else {
            for (const QVariant &v : upcoming) {
                const QVariantMap item = v.toMap();
                if (item.value(QStringLiteral("timeless")).toBool())
                    continue;
                const QTime t = QDateTime::fromMSecsSinceEpoch(item.value(QStringLiteral("start")).toLongLong(), m_app->zone()).time();
                subtitle = i18n("Next: %1 at %2", item.value(QStringLiteral("title")).toString(),
                                QLocale().toString(t, QLocale::ShortFormat));
                break;
            }
        }
        if (subtitle.isEmpty())
            subtitle = i18n("Nothing scheduled for the rest of today");
    }
    m_item->setToolTip(QStringLiteral("org.kde.tasktumbler"), i18n("Task Tumbler"), subtitle);
}

} // namespace tumbler
