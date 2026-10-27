#include "webshell.h"

#include "appcontroller.h"
#include "appscheme.h"
#include "shellbridge.h"

#include <QCloseEvent>
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

    auto *page = new QWebEnginePage(profile, this);
    page->settings()->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, true);
    page->settings()->setAttribute(QWebEngineSettings::ShowScrollBars, true);

    m_bridge = new ShellBridge(this, options.sandboxed, this);
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

    m_view->setUrl(options.devUrl.isEmpty() ? QUrl(QStringLiteral("tumbler://app/")) : options.devUrl);
}

WebShell::~WebShell()
{
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

} // namespace tumbler
