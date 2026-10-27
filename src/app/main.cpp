#include "appcontroller.h"
#include "database.h"
#include "desktop/autostart.h"
#include "desktop/reminders.h"
#include "desktop/externalapi.h"
#include "desktop/hotkey.h"
#include "desktop/idlewatch.h"
#include "desktop/launcher.h"
#include "desktop/tray.h"
#ifdef TUMBLER_HAVE_DBUS
#include "desktop/focusguard.h"
#include "desktop/krunner.h"
#endif
#include "ipc.h"
#include "mcpserver.h"
#include "socketbackend.h"
#include "keystore.h"
#include "remote/remotehub.h"
#include "sync/synchub.h"
#include "web/appscheme.h"
#include "web/webshell.h"

#include <KAboutData>
#include <KLocalizedString>
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#ifdef TUMBLER_HAVE_DBUS
#include <QDBusConnection>
#endif
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include <QTimer>
#include <QWebEngineView>
#include <memory>

using namespace tumbler;

namespace {
// `task-tumbler --mcp [--db path]`: a Model Context Protocol server on
// stdin/stdout for agents like Claude Code. No GUI; it drives the running app
// over its local socket.
int runMcp(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QString db;
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--db") && i + 1 < args.size())
            db = args[i + 1];
        else if (args[i].startsWith(QLatin1String("--db=")))
            db = args[i].mid(5);
    }
    tumbler::mcp::SocketBackend backend(db);
    tumbler::mcp::Server server(&backend);
    return tumbler::mcp::runStdio(server);
}

#ifdef Q_OS_MACOS
// KNotifications looks for tasktumbler.notifyrc in the data locations, and
// on macOS those don't include the app bundle. Keep a copy in Application
// Support, refreshed when the bundled one changes.
void installNotifyConfig()
{
    QFile bundled(QStringLiteral(":/notifyrc/tasktumbler.notifyrc"));
    if (!bundled.open(QIODevice::ReadOnly))
        return;
    const QByteArray config = bundled.readAll();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/knotifications6");
    QFile installed(dir + QStringLiteral("/tasktumbler.notifyrc"));
    if (installed.open(QIODevice::ReadOnly) && installed.readAll() == config)
        return;
    installed.close();
    QDir().mkpath(dir);
    if (installed.open(QIODevice::WriteOnly | QIODevice::Truncate))
        installed.write(config);
}
#endif
} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--mcp") == 0)
            return runMcp(argc, argv);
    }

#ifdef TUMBLER_DEV_DATA_DIR
    // Running from the build tree: let KNotification find our notifyrc.
    if (QDir(QStringLiteral(TUMBLER_DEV_DATA_DIR)).exists()) {
        // Unset means the XDG default, which must stay on the list.
        const QByteArray dirs = qEnvironmentVariableIsEmpty("XDG_DATA_DIRS") ? QByteArray("/usr/local/share:/usr/share") : qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", QByteArray(TUMBLER_DEV_DATA_DIR ":") + dirs);
    }
#endif

    // The web UI's scheme must be known before the engine starts.
    AppScheme::registerScheme();
    // No GPU offscreen (screenshot runs): Chromium draws in software.
    if (qgetenv("QT_QPA_PLATFORM") == "offscreen" && qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");

    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("tasktumbler");
#ifdef Q_OS_MACOS
    installNotifyConfig();
#endif

#ifndef Q_OS_MACOS
    // Desktops without a platform theme (and offscreen runs) name no icon
    // theme or places to look, and then no app icon is found at all.
    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeSearchPaths(QIcon::themeSearchPaths()
                                   + QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"),
                                                               QStandardPaths::LocateDirectory));
        QIcon::setThemeName(QStringLiteral("hicolor"));
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    }
#endif

    KAboutData about(QStringLiteral("tasktumbler"), i18n("Task Tumbler"), QStringLiteral("0.1.0"),
                     i18n("Energy-aware tasks and scheduling"), KAboutLicense::MIT);
    about.setDesktopFileName(QStringLiteral("org.kde.tasktumbler"));
    about.setOrganizationDomain("kde.org");
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("org.kde.tasktumbler"), QIcon(QStringLiteral(":/icons/org.kde.tasktumbler.svg"))));

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption captureOption(QStringLiteral("capture"), i18n("Open quick capture."));
    const QCommandLineOption hiddenOption(QStringLiteral("hidden"), i18n("Start in the system tray."));
    const QCommandLineOption addOption(QStringLiteral("add"), i18n("Capture a task (quick-capture syntax) and exit."), QStringLiteral("text"));
    const QCommandLineOption mcpOption(QStringLiteral("mcp"), i18n("Serve the Model Context Protocol on stdin/stdout for AI agents."));
    const QCommandLineOption dbOption(QStringLiteral("db"), i18n("Use this database file (for testing: no login item, KRunner plugin or D-Bus name)."), QStringLiteral("path"));
    parser.addOption(captureOption);
    parser.addOption(hiddenOption);
    parser.addOption(dbOption);
    parser.addOption(addOption);
    parser.addOption(mcpOption);
    const QCommandLineOption devUrlOption(QStringLiteral("dev-url"), i18n("Load the web interface from this dev server."), QStringLiteral("url"));
    parser.addOption(devUrlOption);
    parser.process(app);
    about.processCommandLine(&parser);

    // Screenshot mode (development) must not touch the user's session: no
    // tray icon, global shortcut, notifications, idle watching, keyring or
    // bus names.
    const QString shot = qEnvironmentVariable("TUMBLER_SCREENSHOT");
    const bool sandboxed = !shot.isEmpty();
    const QString dbArg = parser.isSet(dbOption) ? parser.value(dbOption) : QString();

    // One instance per database. A second launch hands its arguments to the
    // running one and exits. A --db instance has a socket of its own, so it
    // runs alongside the real one and `--mcp --db` can reach it; that's also
    // the only way a screenshot run listens at all.
    const QString socketPath = ipc::socketPath(dbArg);
    const bool listens = !sandboxed || !dbArg.isEmpty();
    if (listens) {
        ipc::Client running(socketPath);
        if (running.connect(500)) {
            // The launcher's activation token lets the running window come to the front on Wayland.
            QVariantList args;
            for (const QString &a : QCoreApplication::arguments())
                args.append(a);
            if (const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN"); !token.isEmpty())
                args.append(QStringLiteral("--activation-token=") + token);
            QString handoff;
            running.call(QStringLiteral("Activate"), args, &handoff, 5000);
            if (!handoff.isEmpty())
                qCritical("Task Tumbler is already running but didn't answer: %s", qPrintable(handoff));
            return handoff.isEmpty() ? 0 : 1;
        }
    }

    Database db;
    QString error;
    const QString path = parser.isSet(dbOption) ? parser.value(dbOption) : Database::defaultPath();
    if (!db.open(path, &error) || !db.migrate(&error)) {
        qCritical("Cannot open %s: %s", qPrintable(path), qPrintable(error));
        return 1;
    }

    KeyStore keys;
    AppController controller(db, nullptr, sandboxed ? nullptr : &keys);
    AppController::setInstance(&controller);
    // The phone companion. Off until enabled in Settings; in screenshot mode
    // its secrets go to a throwaway file.
    RemoteHub hub(&controller, sandboxed ? QDir::tempPath() + QStringLiteral("/tumbler-remote-%1.json").arg(QCoreApplication::applicationPid())
                                         : RemoteHub::defaultSecretsPath());
    RemoteHub::setInstance(&hub);
    // Linear and calendar subscriptions. In screenshot mode nothing syncs on
    // its own and the keyring is a stub that holds nothing.
    NullKeyStore noKeys;
    SyncHub syncHub(&controller, sandboxed ? &noKeys : &keys);
    SyncHub::setInstance(&syncHub);
    syncHub.setAutomatic(!sandboxed);
    controller.dailyBackup();

    // The window: the React UI (webui/) in a web view.
    WebShell::Options options;
    options.devUrl = parser.isSet(devUrlOption) ? QUrl::fromUserInput(parser.value(devUrlOption)) : QUrl();
    options.sandboxed = sandboxed;
    auto shell = std::make_unique<WebShell>(&controller, &syncHub, &hub, options);
    shell->winId(); // makes the QWindow the tray hangs on to
    QWindow *window = shell->windowHandle();

    std::unique_ptr<Tray> tray;
    std::unique_ptr<Reminders> reminders;
    std::unique_ptr<IdleWatch> idle;
    if (!sandboxed) {
        tray = std::make_unique<Tray>(&controller, window);
        reminders = std::make_unique<Reminders>(&controller, &hub);
        idle = std::make_unique<IdleWatch>(&controller);
    }
    Launcher launcher(&controller);
    ExternalApi api(&controller);
    ipc::Server socket(&api, [&api] { return api.takeError(); });
    if (listens) {
        QString socketError;
        if (!socket.listen(socketPath, &socketError))
            qWarning("Not listening for other apps: %s", qPrintable(socketError));
    }
#ifdef TUMBLER_HAVE_DBUS
    // Do Not Disturb while focusing, scripts on the session bus, and KRunner.
    std::unique_ptr<FocusGuard> focus;
    if (!sandboxed)
        focus = std::make_unique<FocusGuard>(&controller);
    KRunnerApi runner(&controller);
    if (!sandboxed && dbArg.isEmpty()) {
        // The test instance (--db) stays off the bus so it can't be mistaken for the real one.
        QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.tasktumbler"));
        api.registerOnBus();
        runner.registerOnBus();
    }
#endif

    // The login item and the KRunner plugin belong to the real instance only:
    // a --db instance's settings mustn't rewrite the user's desktop files.
    const bool ownsSession = dbArg.isEmpty() && !sandboxed;

    // Global quick-capture hotkey. Registering it changes nothing on disk
    // and keeps a binding the user chose, so a --db instance gets it too.
    QAction captureAction(i18n("Quick Capture"));
    captureAction.setObjectName(QStringLiteral("quick_capture"));
    if (!sandboxed)
        registerGlobalShortcut(&captureAction);
    QObject::connect(&captureAction, &QAction::triggered, &controller, &AppController::showCaptureRequested);

    QObject::connect(&socket, &ipc::Server::activateRequested, &controller, [&controller](const QStringList &args) {
        // A second launch hands its arguments to this instance.
        for (const QString &a : args) {
            if (a.startsWith(QLatin1String("--activation-token=")))
                qputenv("XDG_ACTIVATION_TOKEN", a.mid(19).toUtf8()); // used by the next window activation
        }
        const qsizetype add = args.indexOf(QStringLiteral("--add"));
        if (add >= 0 && add + 1 < args.size()) {
            controller.capture(args.at(add + 1));
            return;
        }
        for (const QString &a : args) {
            if (a.startsWith(QLatin1String("--add="))) {
                controller.capture(a.mid(6));
                return;
            }
        }
        if (args.contains(QStringLiteral("--capture")))
            Q_EMIT controller.showCaptureRequested();
        else
            Q_EMIT controller.showWindowRequested();
    });

    // Keep the login item and the KRunner plugin in step with their settings.
    const auto syncDesktopFiles = [&controller] {
        const bool wanted = controller.settings().value(QStringLiteral("autostart")).toBool();
        if (wanted != isAutostartEnabled())
            setAutostart(wanted);
#ifdef TUMBLER_HAVE_DBUS
        KRunnerApi::setPluginInstalled(controller.settings().value(QStringLiteral("krunner")).toBool());
#endif
    };
    if (ownsSession) {
        QObject::connect(&controller, &AppController::settingsChanged, &controller, syncDesktopFiles);
        syncDesktopFiles();
    }

    // First launch with --add: capture and stay in the tray.
    if (parser.isSet(addOption)) {
        controller.capture(parser.value(addOption));
    } else if (!parser.isSet(hiddenOption)) {
        shell->show();
    }
    if (parser.isSet(captureOption))
        Q_EMIT controller.showCaptureRequested();

    // Development aid: TUMBLER_SCREENSHOT=out.png [TUMBLER_PAGE=tasks] [TUMBLER_SHOT_SIZE=WxH]
    // [TUMBLER_COLOR_SCHEME=dark] renders a page, saves it and quits. Use with
    // QT_QPA_PLATFORM=offscreen. "today|js:…" runs steps 700 ms apart, like
    // clicking through.
    if (sandboxed) {
        // Steps once the page has loaded, then a grab of the window.
        const QStringList steps = qEnvironmentVariable("TUMBLER_PAGE", QStringLiteral("today")).split(QLatin1Char('|'));
        const QStringList size = qEnvironmentVariable("TUMBLER_SHOT_SIZE").split(QLatin1Char('x'));
        if (size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0)
            shell->resize(size[0].toInt(), size[1].toInt());
        shell->show();
        const int delay = qEnvironmentVariableIntValue("TUMBLER_SHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("TUMBLER_SHOT_DELAY") : 1500;
        QObject::connect(shell.get(), &WebShell::loaded, shell.get(), [&shell, steps, delay, shot](bool ok) {
            if (!ok)
                qWarning("The web UI didn't load");
            for (qsizetype i = 0; i < steps.size(); ++i)
                QTimer::singleShot(300 + int(i) * 700, shell.get(), [&shell, step = steps.at(i)] {
                    // "js:<code>" runs in the page (clicks, calls), "capture" opens quick
                    // capture, "capture-js:<code>" runs in its page; anything else is a page name.
                    if (step == QLatin1String("capture"))
                        shell->capture()->open();
                    else if (step.startsWith(QLatin1String("capture-js:")))
                        shell->capture()->view()->page()->runJavaScript(step.mid(11));
                    else if (step.startsWith(QLatin1String("js:")))
                        shell->view()->page()->runJavaScript(step.mid(3));
                    else
                        shell->showPage(step);
                });
            QTimer::singleShot(delay + int(steps.size() - 1) * 700, shell.get(), [&shell, shot] {
                shell->grab().save(shot);
                // The quick-capture window, when open, goes next to it.
                if (shell->capture()->isVisible()) {
                    QString name = shot;
                    name.insert(name.lastIndexOf(QLatin1Char('.')), QStringLiteral("-quick-capture"));
                    shell->capture()->grab().save(name);
                }
                QCoreApplication::quit();
            });
        });
        QTimer::singleShot(30000, &app, [] {
            qCritical("Screenshot timed out");
            QCoreApplication::exit(1);
        });
    }

    const int result = app.exec();
    if (sandboxed)
        QFile::remove(QDir::tempPath() + QStringLiteral("/tumbler-remote-%1.json").arg(QCoreApplication::applicationPid()));
    return result;
}
