#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace tumbler {

namespace {
QString autostartFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/org.kde.tasktumbler.desktop");
}
} // namespace

bool setAutostart(bool enabled, QString *error)
{
    const QString path = autostartFile();
    if (!enabled)
        return !QFile::exists(path) || QFile::remove(path);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const QString exec = QCoreApplication::applicationFilePath();
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Task Tumbler\n"
                           "Exec=\"%1\" --hidden\n"
                           "Icon=org.kde.tasktumbler\n"
                           "X-GNOME-Autostart-enabled=true\n"
                           "NoDisplay=true\n")
                .arg(exec)
                .toUtf8());
    return true;
}

bool isAutostartEnabled()
{
    return QFile::exists(autostartFile());
}

} // namespace tumbler
