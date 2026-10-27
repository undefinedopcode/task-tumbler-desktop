#include "shellbridge.h"

#include "appcontroller.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QPalette>
#include <QStyleHints>
#include <QUrl>
#include <QWidget>

namespace tumbler {

ShellBridge::ShellBridge(AppController *app, QWidget *window, bool sandboxed, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_window(window)
    , m_sandboxed(sandboxed)
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &ShellBridge::colorSchemeChanged);
    // Settings → Appearance (or TUMBLER_COLOR_SCHEME=dark|light, for
    // screenshots) overrides the system. Not every platform takes the request
    // (offscreen doesn't), so colorScheme() answers it too.
    connect(app, &AppController::settingsChanged, this, &ShellBridge::applyAppearance);
    applyAppearance();
}

void ShellBridge::applyAppearance()
{
    QString wanted = qEnvironmentVariable("TUMBLER_COLOR_SCHEME");
    if (wanted != QLatin1String("dark") && wanted != QLatin1String("light"))
        wanted = m_app->settings().value(QStringLiteral("appearance")).toString();
    const QString forced = wanted == QLatin1String("dark") || wanted == QLatin1String("light") ? wanted : QString();
    if (forced == m_forced)
        return;
    m_forced = forced;
    QGuiApplication::styleHints()->setColorScheme(forced == QLatin1String("dark")    ? Qt::ColorScheme::Dark
                                                  : forced == QLatin1String("light") ? Qt::ColorScheme::Light
                                                                                     : Qt::ColorScheme::Unknown);
    Q_EMIT colorSchemeChanged();
}

QString ShellBridge::platform() const
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("mac");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

QString ShellBridge::colorScheme() const
{
    if (!m_forced.isEmpty())
        return m_forced;
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
        return QStringLiteral("dark");
    case Qt::ColorScheme::Light:
        return QStringLiteral("light");
    default:
        // No answer from the platform: judge by the palette.
        return QGuiApplication::palette().window().color().lightness() < 128 ? QStringLiteral("dark") : QStringLiteral("light");
    }
}

QString ShellBridge::accent() const
{
    return QGuiApplication::palette().color(QPalette::Accent).name();
}

void ShellBridge::hideWindow()
{
    m_window->hide();
}

void ShellBridge::quit()
{
    QCoreApplication::quit();
}

QString ShellBridge::chooseSaveFile(const QString &title, const QString &suggestedName, const QString &filter)
{
    const QUrl url = QFileDialog::getSaveFileUrl(m_window, title, QUrl::fromLocalFile(suggestedName), filter);
    return url.isEmpty() ? QString() : url.toString();
}

QString ShellBridge::chooseOpenFile(const QString &title, const QString &filter)
{
    const QUrl url = QFileDialog::getOpenFileUrl(m_window, title, QUrl(), filter);
    return url.isEmpty() ? QString() : url.toString();
}

void ShellBridge::openExternal(const QString &url)
{
    const QUrl u(url);
    static const QStringList allowed{QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("mailto"),
                                     QStringLiteral("x-apple.systempreferences")};
    if (!allowed.contains(u.scheme()))
        return;
    if (m_sandboxed)
        qInfo("Would open %s", qPrintable(url));
    else
        QDesktopServices::openUrl(u);
}

} // namespace tumbler
