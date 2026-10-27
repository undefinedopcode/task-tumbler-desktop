#include "webshell.h"

#include "appcontroller.h"
#include "appscheme.h"
#include "shellbridge.h"

#include <QCloseEvent>
#include <QApplication>
#include <QKeySequence>
#include <QMenuBar>
#include <QScreen>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QCursor>

namespace tumbler {

namespace {

// qwebchannel.js, from QtWebChannel's resources, put in every page before it
// runs: works the same for the built UI and the dev server.
QWebEngineScript channelScript()
{
    QFile file(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("qwebchannel.js is missing from QtWebChannel's resources");
    QWebEngineScript script;
    script.setName(QStringLiteral("qwebchannel"));
    script.setSourceCode(QString::fromUtf8(file.readAll()));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    return script;
}

// The UI stays on its own pages: links elsewhere (a note's, say) open in the
// browser instead of replacing the app, and so do new windows.
class ShellPage : public QWebEnginePage
{
public:
    ShellPage(QWebEngineProfile *profile, const QUrl &devUrl, bool sandboxed, QObject *parent)
        : QWebEnginePage(profile, parent)
        , m_devUrl(devUrl)
        , m_sandboxed(sandboxed)
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (url.scheme() == QLatin1String(AppScheme::name) || url.scheme() == QLatin1String("data") || url.scheme() == QLatin1String("about"))
            return true;
        if (!m_devUrl.isEmpty() && url.scheme() == m_devUrl.scheme() && url.host() == m_devUrl.host() && url.port() == m_devUrl.port())
            return true;
        if (isMainFrame && type == NavigationTypeLinkClicked)
            openOutside(url, m_sandboxed);
        return false;
    }

    QWebEnginePage *createWindow(WebWindowType) override
    {
        // target="_blank" and window.open(): a page that only hands its first
        // URL to the browser, then goes.
        auto *page = new QWebEnginePage(profile(), this);
        connect(page, &QWebEnginePage::urlChanged, page, [page, sandboxed = m_sandboxed](const QUrl &url) {
            openOutside(url, sandboxed);
            page->deleteLater();
        });
        return page;
    }

private:
    // Screenshot runs only say so: they mustn't open things in the session.
    static void openOutside(const QUrl &url, bool sandboxed)
    {
        static const QStringList allowed{QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("mailto")};
        if (!allowed.contains(url.scheme()))
            return;
        if (sandboxed)
            qInfo("Would open %s", qPrintable(url.toString()));
        else
            QDesktopServices::openUrl(url);
    }

    QUrl m_devUrl;
    bool m_sandboxed;
};

} // namespace

WebShell::WebShell(AppController *app, QObject *sync, QObject *remote, const Options &options, QWidget *parent)
    : QMainWindow(parent)
    , m_app(app)
{
    setWindowTitle(QStringLiteral("Task Tumbler"));
    resize(1100, 760);
    setMinimumSize(720, 520);

    // Its own profile: the UI's local storage (remembered tabs and the like)
    // lives with the app's data. Screenshot runs keep nothing.
    auto *profile = options.sandboxed ? new QWebEngineProfile(this) : new QWebEngineProfile(QStringLiteral("ui"), this);
    if (!options.sandboxed)
        profile->setPersistentStoragePath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/webui"));
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->installUrlSchemeHandler(AppScheme::name, new AppScheme(profile));
    profile->scripts()->insert(channelScript());

    auto *page = new ShellPage(profile, options.devUrl, options.sandboxed, this);
    page->settings()->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
    page->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, true);

    m_bridge = new ShellBridge(app, this, options.sandboxed, this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("app"), app);
    m_channel->registerObject(QStringLiteral("shell"), m_bridge);
    if (sync)
        m_channel->registerObject(QStringLiteral("sync"), sync);
    if (remote)
        m_channel->registerObject(QStringLiteral("remote"), remote);
    page->setWebChannel(m_channel);

    m_view = new QWebEngineView(this);
    m_view->setPage(page);
    // Matches the UI's background, so there's no white flash before it paints.
    page->setBackgroundColor(m_bridge->colorScheme() == QLatin1String("dark") ? QColor(0x17, 0x17, 0x1a) : QColor(0xfa, 0xfa, 0xf9));
    m_view->setContextMenuPolicy(options.devUrl.isEmpty() ? Qt::NoContextMenu : Qt::DefaultContextMenu);
    setCentralWidget(m_view);
    connect(m_view, &QWebEngineView::loadFinished, this, &WebShell::loaded);

    connect(app, &AppController::showWindowRequested, this, &WebShell::raiseWindow);
    connect(app, &AppController::focusModeChanged, this, [this] {
        if (m_app->focusMode())
            raiseWindow();
    });

    const QUrl base = options.devUrl.isEmpty() ? QUrl(QStringLiteral("tumbler://app/")) : options.devUrl;
    m_view->setUrl(base);

    m_capture = new CaptureWindow(app, profile, base, options.sandboxed);
    connect(app, &AppController::showCaptureRequested, m_capture, &CaptureWindow::open);
    // On macOS a quit (menu, Dock, tray, logout) stalls while quick capture
    // is open, so it goes first.
    qApp->installEventFilter(this);

#ifdef Q_OS_MACOS
    buildMenuBar();
#endif
}

WebShell::~WebShell()
{
    qApp->removeEventFilter(this);
    delete m_capture; // its page shares this window's profile
    m_capture = nullptr;
    // The view before its page, and the page before its profile: as plain
    // children they'd go in the order they were made, profile first.
    QWebEnginePage *page = m_view->page();
    QWebEngineProfile *profile = page->profile();
    delete m_view;
    delete page;
    delete profile;
}

void WebShell::showPage(const QString &name)
{
    const QString arg = QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact));
    m_view->page()->runJavaScript(QStringLiteral("window.tumbler && window.tumbler.showPage(...%1)").arg(arg));
}

void WebShell::callPage(const QString &call)
{
    m_view->page()->runJavaScript(QStringLiteral("window.tumbler && window.tumbler.%1").arg(call));
}

void WebShell::buildMenuBar()
{
    // Built everywhere (so it compiles), shown only on macOS, where it is the
    // system menu bar. Undo goes through the page: in a text field it's the
    // field's own undo, elsewhere the app's.
    QMenuBar *bar = menuBar();
    const auto add = [this](QMenu *menu, const QString &text, const QKeySequence &keys, auto &&slot) {
        QAction *a = menu->addAction(text);
        a->setShortcut(keys);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    QWebEnginePage *page = m_view->page();

    QMenu *app = bar->addMenu(QStringLiteral("Task Tumbler"));
    add(app, QStringLiteral("Settings…"), QKeySequence::Preferences, [this] {
        raiseWindow();
        showPage(QStringLiteral("settings"));
    })->setMenuRole(QAction::PreferencesRole);
    add(app, QStringLiteral("Quit Task Tumbler"), QKeySequence::Quit, [] { QCoreApplication::quit(); })->setMenuRole(QAction::QuitRole);

    QMenu *file = bar->addMenu(QStringLiteral("File"));
    add(file, QStringLiteral("New Task"), QKeySequence(Qt::CTRL | Qt::Key_N), [this] {
        raiseWindow();
        callPage(QStringLiteral("newTask()"));
    });
    add(file, QStringLiteral("New Note"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] {
        raiseWindow();
        callPage(QStringLiteral("newNote()"));
    });
    add(file, QStringLiteral("Quick Capture…"), QKeySequence(), [this] { Q_EMIT m_app->showCaptureRequested(); });
    file->addSeparator();
    add(file, QStringLiteral("Close Window"), QKeySequence::Close, [this] { close(); });

    QMenu *edit = bar->addMenu(QStringLiteral("Edit"));
    add(edit, QStringLiteral("Undo"), QKeySequence::Undo, [this] { callPage(QStringLiteral("undo()")); });
    add(edit, QStringLiteral("Redo"), QKeySequence::Redo, [this] { callPage(QStringLiteral("redo()")); });
    edit->addSeparator();
    add(edit, QStringLiteral("Cut"), QKeySequence::Cut, [page] { page->triggerAction(QWebEnginePage::Cut); });
    add(edit, QStringLiteral("Copy"), QKeySequence::Copy, [page] { page->triggerAction(QWebEnginePage::Copy); });
    add(edit, QStringLiteral("Paste"), QKeySequence::Paste, [page] { page->triggerAction(QWebEnginePage::Paste); });
    add(edit, QStringLiteral("Select All"), QKeySequence::SelectAll, [page] { page->triggerAction(QWebEnginePage::SelectAll); });
    edit->addSeparator();
    add(edit, QStringLiteral("Find"), QKeySequence::Find, [this] { callPage(QStringLiteral("find()")); });

    QMenu *go = bar->addMenu(QStringLiteral("Go"));
    const QList<QPair<QString, QString>> pages{{QStringLiteral("Today"), QStringLiteral("today")},
                                               {QStringLiteral("Tasks"), QStringLiteral("tasks")},
                                               {QStringLiteral("Schedule"), QStringLiteral("schedule")},
                                               {QStringLiteral("Notes"), QStringLiteral("notes")}};
    for (qsizetype i = 0; i < pages.size(); ++i) {
        add(go, pages[i].first, QKeySequence(Qt::CTRL | (Qt::Key_1 + int(i))), [this, name = pages[i].second] {
            raiseWindow();
            showPage(name);
        });
    }
    go->addSeparator();
    for (const auto &[label, name] : {std::pair{QStringLiteral("Energy"), QStringLiteral("energy")}, {QStringLiteral("Stats"), QStringLiteral("stats")},
                                      {QStringLiteral("Categories"), QStringLiteral("categories")}, {QStringLiteral("Close Out the Day"), QStringLiteral("closeout")},
                                      {QStringLiteral("Week Review"), QStringLiteral("review")}}) {
        add(go, label, QKeySequence(), [this, name] {
            raiseWindow();
            showPage(name);
        });
    }

    QMenu *view = bar->addMenu(QStringLiteral("View"));
    add(view, QStringLiteral("Plan My Day…"), QKeySequence(Qt::CTRL | Qt::Key_P), [this] {
        raiseWindow();
        callPage(QStringLiteral("openPlan()"));
    });
    add(view, QStringLiteral("Enter Full Screen"), QKeySequence::FullScreen, [this] { setWindowState(windowState() ^ Qt::WindowFullScreen); });

    QMenu *window = bar->addMenu(QStringLiteral("Window"));
    add(window, QStringLiteral("Minimize"), QKeySequence(Qt::CTRL | Qt::Key_M), [this] { showMinimized(); });
    add(window, QStringLiteral("Task Tumbler"), QKeySequence(), [this] { raiseWindow(); });
}

void WebShell::raiseWindow()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void WebShell::closeEvent(QCloseEvent *event)
{
    // With "close to tray" on, closing only hides the window.
    if (m_app->settings().value(QStringLiteral("closeToTray")).toBool()) {
        event->ignore();
        hide();
        return;
    }
    QMainWindow::closeEvent(event);
}

bool WebShell::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::Quit && m_capture)
        m_capture->hide();
    return QMainWindow::eventFilter(watched, event);
}

CaptureWindow::CaptureWindow(AppController *app, QWebEngineProfile *profile, const QUrl &base, bool sandboxed)
    : QWidget(nullptr, Qt::Dialog | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(QStringLiteral("Quick Capture"));
    setFixedSize(600, 196);
    auto *page = new ShellPage(profile, base.scheme() == QLatin1String(AppScheme::name) ? QUrl() : base, sandboxed, this);
    auto *channel = new QWebChannel(this);
    channel->registerObject(QStringLiteral("app"), app);
    channel->registerObject(QStringLiteral("shell"), new ShellBridge(app, this, sandboxed, this));
    page->setWebChannel(channel);
    page->setBackgroundColor(Qt::transparent);
    m_view = new QWebEngineView(this);
    m_view->setPage(page);
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);
    QUrl url = base;
    url.setFragment(QStringLiteral("capture"));
    m_view->setUrl(url);
}

CaptureWindow::~CaptureWindow()
{
    QWebEnginePage *page = m_view->page();
    delete m_view;
    delete page;
}

void CaptureWindow::open()
{
    // Centred a third of the way down the screen with the mouse on it.
    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos()) ?: QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        move(area.x() + (area.width() - width()) / 2, area.y() + area.height() / 3 - height() / 2);
    }
    show();
    raise();
    activateWindow();
    m_view->setFocus();
    m_view->page()->runJavaScript(QStringLiteral("window.tumbler && window.tumbler.resetCapture && window.tumbler.resetCapture()"));
}

} // namespace tumbler
