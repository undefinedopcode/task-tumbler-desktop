#include "apps.h"

#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QSettings>
#include <QUrl>

namespace tumbler::apps {

namespace {
// The usual places apps live, one level deep (plus /Applications/Utilities).
QList<App> scan()
{
    const QStringList dirs{QStringLiteral("/Applications"), QStringLiteral("/Applications/Utilities"),
                           QStringLiteral("/System/Applications"), QStringLiteral("/System/Applications/Utilities"),
                           QDir::homePath() + QStringLiteral("/Applications")};
    QList<App> out;
    for (const QString &dir : dirs) {
        for (const QFileInfo &bundle : QDir(dir).entryInfoList({QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            // QSettings reads property lists natively on macOS.
            const QSettings info(bundle.filePath() + QStringLiteral("/Contents/Info.plist"), QSettings::NativeFormat);
            const QString id = info.value(QStringLiteral("CFBundleIdentifier")).toString();
            if (id.isEmpty() || info.value(QStringLiteral("LSUIElement")).toBool()) // background helpers
                continue;
            QString name = info.value(QStringLiteral("CFBundleDisplayName")).toString();
            if (name.isEmpty())
                name = bundle.completeBaseName();
            out.append({id, name, bundle.filePath()}); // the web UI asks the system for its icon
        }
    }
    return out;
}
} // namespace

QList<App> installed()
{
    return scan();
}

std::optional<App> find(const QString &id)
{
    static QList<App> cache = scan();
    for (int attempt = 0; attempt < 2; ++attempt) {
        for (const App &a : cache) {
            if (a.id == id)
                return a;
        }
        cache = scan(); // installed since?
    }
    return std::nullopt;
}

void openUrl(const QUrl &url, QObject *, Failed failed)
{
    if (!QDesktopServices::openUrl(url))
        failed(QStringLiteral("nothing is set up to open it"));
}

void launch(const QString &id, QObject *context, Failed failed)
{
    auto *open = new QProcess(context);
    QObject::connect(open, &QProcess::finished, context, [open, failed](int code) {
        if (code != 0)
            failed(QStringLiteral("that app isn't installed"));
        open->deleteLater();
    });
    open->start(QStringLiteral("/usr/bin/open"), {QStringLiteral("-b"), id});
}

} // namespace tumbler::apps
