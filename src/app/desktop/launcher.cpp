#include "launcher.h"

#include "appcontroller.h"
#include "apps.h"

#include <QProcess>
#include <QUrl>

namespace tumbler {

Launcher::Launcher(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    connect(app, &AppController::launchRequested, this, &Launcher::open);
}

void Launcher::open(const QVariantMap &action)
{
    const QString kind = action.value(QStringLiteral("kind")).toString();
    const QString target = action.value(QStringLiteral("target")).toString();
    const QString label = action.value(QStringLiteral("label")).toString();
    const auto failed = [this, label](const QString &why) {
        Q_EMIT m_app->message(QStringLiteral("Couldn't open %1: %2").arg(label, why));
    };

    if (kind == QLatin1String("url") || kind == QLatin1String("file")) {
        apps::openUrl(kind == QLatin1String("file") ? QUrl::fromLocalFile(target) : QUrl(target), this, failed);
    } else if (kind == QLatin1String("app")) {
        apps::launch(target, this, failed);
    } else if (kind == QLatin1String("command")) {
        // argv straight to the program: no shell.
        QStringList argv = action.value(QStringLiteral("argv")).toStringList();
        if (argv.isEmpty())
            return;
        const QString program = argv.takeFirst();
        if (!QProcess::startDetached(program, argv))
            failed(QStringLiteral("%1 didn't start").arg(program));
    }
}

} // namespace tumbler
