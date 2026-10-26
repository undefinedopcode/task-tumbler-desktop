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

#include <KAboutData>
#include <KLocalizedQmlContext>
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
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
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
} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--mcp") == 0)
            return runMcp(argc, argv);
    }

#ifdef TUMBLER_DEV_DATA_DIR
    // Running from the build tree: let KNotification find our notifyrc.
    if (QDir(QStringLiteral(TUMBLER_DEV_DATA_DIR)).exists())
        qputenv("XDG_DATA_DIRS", QByteArray(TUMBLER_DEV_DATA_DIR ":") + qgetenv("XDG_DATA_DIRS"));
#endif

    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("tasktumbler");

    KAboutData about(QStringLiteral("tasktumbler"), i18n("Task Tumbler"), QStringLiteral("0.1.0"),
                     i18n("Energy-aware tasks and scheduling"), KAboutLicense::MIT);
    about.setDesktopFileName(QStringLiteral("org.kde.tasktumbler"));
    about.setOrganizationDomain("kde.org");
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("org.kde.tasktumbler"), QIcon(QStringLiteral(":/icons/org.kde.tasktumbler.svg"))));

#ifndef Q_OS_MACOS
    // Plasma's own look. On macOS Kirigami picks its style itself.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
#endif

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    const QCommandLineOption captureOption(QStringLiteral("capture"), i18n("Open quick capture."));
    const QCommandLineOption hiddenOption(QStringLiteral("hidden"), i18n("Start in the system tray."));
    const QCommandLineOption addOption(QStringLiteral("add"), i18n("Capture a task (quick-capture syntax) and exit."), QStringLiteral("text"));
    const QCommandLineOption mcpOption(QStringLiteral("mcp"), i18n("Serve the Model Context Protocol on stdin/stdout for AI agents."));
    const QCommandLineOption dbOption(QStringLiteral("db"), i18n("Use this database file (for testing)."), QStringLiteral("path"));
    parser.addOption(captureOption);
    parser.addOption(hiddenOption);
    parser.addOption(dbOption);
    parser.addOption(addOption);
    parser.addOption(mcpOption);
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

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("org.kde.tasktumbler", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());

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
    if (dbArg.isEmpty() && !sandboxed) {
        // The test instance (--db) stays off the bus so it can't be mistaken for the real one.
        QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.tasktumbler"));
        api.registerOnBus();
        runner.registerOnBus();
    }
#endif

    // Global quick-capture hotkey.
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
    if (!sandboxed) {
        QObject::connect(&controller, &AppController::settingsChanged, &controller, syncDesktopFiles);
        syncDesktopFiles();
    }

    // First launch with --add: capture and stay in the tray.
    if (parser.isSet(addOption)) {
        controller.capture(parser.value(addOption));
    } else if (!parser.isSet(hiddenOption)) {
        window->show();
    }
    if (parser.isSet(captureOption))
        Q_EMIT controller.showCaptureRequested();

    // Development aid: TUMBLER_SCREENSHOT=out.png [TUMBLER_PAGE=tasks] [TUMBLER_SHOT_SIZE=WxH]
    // renders a page, saves it and quits. Use with QT_QPA_PLATFORM=offscreen.
    // "today|plan" runs steps 700 ms apart, like clicking through.
    if (sandboxed) {
        const QStringList steps = qEnvironmentVariable("TUMBLER_PAGE", QStringLiteral("today")).split(QLatin1Char('|'));
        // TUMBLER_SHOT_SIZE=1100x2400 for a taller look at long pages.
        const QStringList size = qEnvironmentVariable("TUMBLER_SHOT_SIZE").split(QLatin1Char('x'));
        if (size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0)
            window->resize(size[0].toInt(), size[1].toInt());
        window->show();
        for (qsizetype i = 0; i < steps.size(); ++i) {
            QTimer::singleShot(300 + int(i) * 700, window, [window, step = steps.at(i)] {
                if (step == QLatin1String("plan"))
                    QMetaObject::invokeMethod(window, "openPlan");
                else
                    QMetaObject::invokeMethod(window, "showPage", Q_ARG(QVariant, step));
            });
        }
        // TUMBLER_SHOT_DELAY keeps a sandboxed instance up longer (e.g. to
        // point a headless browser at its phone server).
        const int delay = qEnvironmentVariableIntValue("TUMBLER_SHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("TUMBLER_SHOT_DELAY") : 1500;
        QTimer::singleShot(delay + int(steps.size() - 1) * 700, window, [window, shot] {
            window->grabWindow().save(shot);
            // TUMBLER_PROBE=x,y lists the visible items covering that point.
            const QStringList probe = qEnvironmentVariable("TUMBLER_PROBE").split(QLatin1Char(','));
            if (probe.size() == 2) {
                const QPointF pt(probe[0].toDouble(), probe[1].toDouble());
                std::function<void(QQuickItem *, int)> walk = [&](QQuickItem *item, int depth) {
                    if (!item->isVisible() || item->opacity() == 0)
                        return;
                    const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                    if (r.contains(pt) && r.width() < 400)
                        qInfo().noquote() << QString(depth, QLatin1Char(' ')) << item->metaObject()->className() << item->objectName() << r;
                    for (QQuickItem *c : item->childItems())
                        walk(c, depth + 1);
                };
                walk(window->contentItem(), 0);
            }
            // Other open windows (quick capture) go next to it.
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                auto *qw = qobject_cast<QQuickWindow *>(w);
                if (qw && qw != window && qw->isVisible()) {
                    QString name = shot;
                    name.insert(name.lastIndexOf(QLatin1Char('.')), QLatin1Char('-') + qw->title().toLower().replace(QLatin1Char(' '), QLatin1Char('-')));
                    qw->grabWindow().save(name);
                }
            }
            QCoreApplication::quit();
        });
    }

    const int result = app.exec();
    if (sandboxed)
        QFile::remove(QDir::tempPath() + QStringLiteral("/tumbler-remote-%1.json").arg(QCoreApplication::applicationPid()));
    return result;
}
