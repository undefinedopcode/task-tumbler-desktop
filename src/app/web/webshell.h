#pragma once

#include <QMainWindow>
#include <QUrl>

class QWebChannel;
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

Q_SIGNALS:
    void loaded(bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    AppController *m_app;
    QWebEngineView *m_view;
    QWebChannel *m_channel;
    ShellBridge *m_bridge;
};

} // namespace tumbler
