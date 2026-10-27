#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <optional>

class QObject;
class QUrl;

namespace tumbler::apps {

// Installed apps a task's launcher can point at. On Linux the id is the
// desktop entry name (org.kde.kate); on macOS the bundle identifier
// (com.apple.TextEdit). Implemented in apps_linux.cpp / apps_mac.cpp.
struct App {
    QString id;
    QString name;
    QString icon; // an icon theme name (Linux) or the app bundle's path (macOS)
};

QList<App> installed();
std::optional<App> find(const QString &id);

// Open the desktop's way. `failed` is called (maybe later, on `context`'s
// thread) with the reason if it doesn't work.
using Failed = std::function<void(const QString &why)>;
void openUrl(const QUrl &url, QObject *context, Failed failed);
void launch(const QString &id, QObject *context, Failed failed);

} // namespace tumbler::apps
