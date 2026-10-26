#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace tumbler {

// A per-user LaunchAgent: launchd starts it (to the menu bar) at login.
namespace {
QString agentFile()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/org.kde.tasktumbler.plist");
}
} // namespace

bool setAutostart(bool enabled, QString *error)
{
    const QString path = agentFile();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                           "<plist version=\"1.0\">\n<dict>\n"
                           "  <key>Label</key><string>org.kde.tasktumbler</string>\n"
                           "  <key>ProgramArguments</key>\n"
                           "  <array><string>%1</string><string>--hidden</string></array>\n"
                           "  <key>RunAtLoad</key><true/>\n"
                           "</dict>\n</plist>\n")
                .arg(QCoreApplication::applicationFilePath().toHtmlEscaped())
                .toUtf8());
    return true;
}

bool isAutostartEnabled()
{
    return QFile::exists(agentFile());
}

} // namespace tumbler
