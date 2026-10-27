#include "shellbridge.h"

#include <QApplication>
#include <QFileDialog>
#include <QPalette>
#include <QStyleHints>
#include <QUrl>
#include <QWidget>

namespace tumbler {

ShellBridge::ShellBridge(QWidget *window, bool sandboxed, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_sandboxed(sandboxed)
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &ShellBridge::colorSchemeChanged);
    // TUMBLER_COLOR_SCHEME=dark|light overrides the system (screenshots).
    // Not every platform takes the request (offscreen doesn't), so it's also
    // answered here.
    const QString forced = qEnvironmentVariable("TUMBLER_COLOR_SCHEME");
    if (forced == QLatin1String("dark") || forced == QLatin1String("light")) {
        m_forced = forced;
        QGuiApplication::styleHints()->setColorScheme(forced == QLatin1String("dark") ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    }
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

} // namespace tumbler
