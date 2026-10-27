#pragma once

#include <QMainWindow>
#include <QUrl>

class QWebChannel;
class QWebEngineProfile;
class QWebEngineView;

namespace tumbler {

class AppController;
class ShellBridge;

// The main window: the React UI (webui/) in a web view, talking to the app
// over a QWebChannel. Published objects: "app" (AppController), "sync"
// (SyncHub), "remote" (RemoteHub) and "shell" (ShellBridge: the window and
// the platform).
class WebShell : public QMainWindow
{
    Q_OBJECT
public:
    struct Options {
        QUrl devUrl;            // a Vite dev server, instead of the built UI
        bool sandboxed = false; // screenshot mode: nothing kept on disk
    };

    WebShell(AppController *app, QObject *sync, QObject *remote, const Options &options, QWidget *parent = nullptr);
    ~WebShell() override;

    QWebEngineView *view() const { return m_view; }
    // Calls window.tumbler.showPage(name) in the page.
    void showPage(const QString &name);
    void raiseWindow();
    class CaptureWindow *capture() const { return m_capture; }
    // Runs window.tumbler.<call> in the page (menu commands).
    void callPage(const QString &call);

Q_SIGNALS:
    void loaded(bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The macOS menu bar (elsewhere the UI has its own navigation).
    void buildMenuBar();

    AppController *m_app;
    QWebEngineView *m_view;
    QWebChannel *m_channel;
    ShellBridge *m_bridge;
    class CaptureWindow *m_capture = nullptr;
};

// Quick capture: a small window over everything with the same UI at
// #capture. Made hidden at startup so the shortcut opens it at once.
class CaptureWindow : public QWidget
{
    Q_OBJECT
public:
    CaptureWindow(AppController *app, QWebEngineProfile *profile, const QUrl &base, bool sandboxed);
    ~CaptureWindow() override;

    void open();
    QWebEngineView *view() const { return m_view; }

private:
    QWebEngineView *m_view;
};

} // namespace tumbler
