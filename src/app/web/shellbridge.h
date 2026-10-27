#pragma once

#include <QObject>
#include <QVariantMap>

class QWidget;

namespace tumbler {
class AppController;
}

namespace tumbler {

// What the page needs from its host beyond the app itself: which platform
// it's on, light or dark, and the window.
class ShellBridge : public QObject
{
    Q_OBJECT
    // "linux" | "mac" | "windows"
    Q_PROPERTY(QString platform READ platform CONSTANT)
    // "light" | "dark", following the system.
    Q_PROPERTY(QString colorScheme READ colorScheme NOTIFY colorSchemeChanged)
    // The system accent colour (#rrggbb), for those who want it.
    Q_PROPERTY(QString accent READ accent NOTIFY colorSchemeChanged)
    Q_PROPERTY(bool sandboxed READ sandboxed CONSTANT)

public:
    ShellBridge(AppController *app, QWidget *window, bool sandboxed, QObject *parent = nullptr);

    QString platform() const;
    QString colorScheme() const;
    QString accent() const;
    bool sandboxed() const { return m_sandboxed; }

public Q_SLOTS:
    void hideWindow();
    void quit();
    // Native file dialogs; answer with the chosen file as a URL string, or "".
    QString chooseSaveFile(const QString &title, const QString &suggestedName, const QString &filter);
    QString chooseOpenFile(const QString &title, const QString &filter);
    // Web, mail and (on macOS) System Settings links, in the system's handler.
    void openExternal(const QString &url);

Q_SIGNALS:
    void colorSchemeChanged();

private:
    // Settings → Appearance, else the system (or TUMBLER_COLOR_SCHEME).
    void applyAppearance();

    AppController *m_app;
    QWidget *m_window;
    bool m_sandboxed;
    QString m_forced;
};

} // namespace tumbler
