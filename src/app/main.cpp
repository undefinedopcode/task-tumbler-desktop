#include "appcontroller.h"
#include "database.h"
#include "desktop/autostart.h"
#include "desktop/reminders.h"
#include "desktop/tray.h"
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

using namespace tumbler;

int main(int argc, char *argv[])
{
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
    const QCommandLineOption dbOption(QStringLiteral("db"), i18n("Use this database file (for testing)."), QStringLiteral("path"));
    parser.addOption(captureOption);
    parser.addOption(hiddenOption);
    parser.addOption(dbOption);
    parser.process(app);
    about.processCommandLine(&parser);

    // One instance per user. A second launch hands its arguments over and
    // exits; a --db test instance runs alongside instead.
    KDBusService service(parser.isSet(dbOption) ? KDBusService::Multiple : KDBusService::Unique);

    Database db;
    QString error;
    const QString path = parser.isSet(dbOption) ? parser.value(dbOption) : Database::defaultPath();
    if (!db.open(path, &error) || !db.migrate(&error)) {
        qCritical("Cannot open %s: %s", qPrintable(path), qPrintable(error));
        return 1;
    }

    KeyStore keys;
    AppController controller(db, nullptr, &keys);
    AppController::setInstance(&controller);
    controller.dailyBackup();

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("org.kde.tasktumbler", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());

    Tray tray(&controller, window);
    Reminders reminders(&controller);

    // Global quick-capture hotkey, rebindable in System Settings > Shortcuts.
    QAction captureAction(i18n("Quick Capture"));
    captureAction.setObjectName(QStringLiteral("quick_capture"));
    KGlobalAccel::self()->setGlobalShortcut(&captureAction, QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_Space));
    QObject::connect(&captureAction, &QAction::triggered, &controller, &AppController::showCaptureRequested);

    QObject::connect(&service, &KDBusService::activateRequested, &controller, [&controller](const QStringList &args, const QString &) {
        if (args.contains(QStringLiteral("--capture")))
            Q_EMIT controller.showCaptureRequested();
        else
            Q_EMIT controller.showWindowRequested();
    });

    // Keep the login item in step with the setting.
    QObject::connect(&controller, &AppController::settingsChanged, &controller, [&controller] {
        const bool wanted = controller.settings().value(QStringLiteral("autostart")).toBool();
        if (wanted != isAutostartEnabled())
            setAutostart(wanted);
    });

    if (!parser.isSet(hiddenOption))
        window->show();
    if (parser.isSet(captureOption))
        Q_EMIT controller.showCaptureRequested();

    // Development aid: TUMBLER_SCREENSHOT=out.png [TUMBLER_PAGE=tasks]
    // renders a page, saves it and quits. Use with QT_QPA_PLATFORM=offscreen.
    // "today|plan" runs steps 700 ms apart, like clicking through.
    const QString shot = qEnvironmentVariable("TUMBLER_SCREENSHOT");
    if (!shot.isEmpty()) {
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
