#include "appcontroller.h"
#include "database.h"
#include "desktop/autostart.h"
#include "desktop/reminders.h"
#include "desktop/dbusapi.h"
#include "desktop/focusguard.h"
#include "desktop/idlewatch.h"
#include "desktop/krunner.h"
#include "desktop/launcher.h"
#include "desktop/tray.h"
#include "dbusbackend.h"
#include "mcpserver.h"
#include "keystore.h"

#include <KAboutData>
#include <KDBusService>
#include <KGlobalAccel>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <memory>

using namespace tumbler;

namespace {
// `task-tumbler --mcp`: a Model Context Protocol server on stdin/stdout for
// agents like Claude Code. No GUI; it drives the running app over D-Bus.
int runMcp(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    tumbler::mcp::DBusBackend backend;
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

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

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

    // One instance per user. A second launch hands its arguments over and
    // exits; a --db test instance runs alongside instead.
    std::unique_ptr<KDBusService> service;
    if (!sandboxed)
        service = std::make_unique<KDBusService>(parser.isSet(dbOption) ? KDBusService::Multiple : KDBusService::Unique);

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
    std::unique_ptr<FocusGuard> focus;
    if (!sandboxed) {
        tray = std::make_unique<Tray>(&controller, window);
        reminders = std::make_unique<Reminders>(&controller);
        idle = std::make_unique<IdleWatch>(&controller);
        focus = std::make_unique<FocusGuard>(&controller);
    }
    Launcher launcher(&controller);
    DBusApi api(&controller);
    KRunnerApi runner(&controller);
    if (!parser.isSet(dbOption) && !sandboxed) {
        // The test instance (--db) stays off the bus so it can't be mistaken for the real one.
        api.registerOnBus();
        runner.registerOnBus();
    }

    // Global quick-capture hotkey, rebindable in System Settings > Shortcuts.
    QAction captureAction(i18n("Quick Capture"));
    captureAction.setObjectName(QStringLiteral("quick_capture"));
    if (!sandboxed)
        KGlobalAccel::self()->setGlobalShortcut(&captureAction, QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_Space));
    QObject::connect(&captureAction, &QAction::triggered, &controller, &AppController::showCaptureRequested);

    if (service) {
        QObject::connect(service.get(), &KDBusService::activateRequested, &controller, [&controller](const QStringList &args, const QString &) {
            // A second launch hands its arguments to this instance.
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
    }

    // Keep the login item and the KRunner plugin in step with their settings.
    const auto syncDesktopFiles = [&controller] {
        const bool wanted = controller.settings().value(QStringLiteral("autostart")).toBool();
        if (wanted != isAutostartEnabled())
            setAutostart(wanted);
        KRunnerApi::setPluginInstalled(controller.settings().value(QStringLiteral("krunner")).toBool());
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

    // Development aid: TUMBLER_SCREENSHOT=out.png [TUMBLER_PAGE=tasks]
    // renders a page, saves it and quits. Use with QT_QPA_PLATFORM=offscreen.
    // "today|plan" runs steps 700 ms apart, like clicking through.
    if (sandboxed) {
        const QStringList steps = qEnvironmentVariable("TUMBLER_PAGE", QStringLiteral("today")).split(QLatin1Char('|'));
        window->show();
        for (qsizetype i = 0; i < steps.size(); ++i) {
            QTimer::singleShot(300 + int(i) * 700, window, [window, step = steps.at(i)] {
                if (step == QLatin1String("plan"))
                    QMetaObject::invokeMethod(window, "openPlan");
                else
                    QMetaObject::invokeMethod(window, "showPage", Q_ARG(QVariant, step));
            });
        }
        QTimer::singleShot(1500 + int(steps.size() - 1) * 700, window, [window, shot] {
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

    return app.exec();
}
