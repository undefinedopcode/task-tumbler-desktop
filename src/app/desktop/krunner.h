#pragma once

#include <QDBusArgument>
#include <QList>
#include <QObject>
#include <QVariantMap>

namespace tumbler {

class AppController;

// org.kde.krunner1 types: a(sssida{sv}) for matches, a(sss) for actions.
struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    int type = 0; // KRunner::QueryMatch::CategoryRelevance-style: 100 exact, 50 possible
    double relevance = 0;
    QVariantMap properties;
};
struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};
using RemoteMatches = QList<RemoteMatch>;
using RemoteActions = QList<RemoteAction>;

// The KRunner plugin, served from the app itself over D-Bus at /runner.
// Plasma finds it through share/krunner/dbusplugins/org.kde.tasktumbler.desktop.
class KRunnerApi : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    explicit KRunnerApi(AppController *app, QObject *parent = nullptr);
    bool registerOnBus();

    // Writes or removes the plugin file in ~/.local/share/krunner/dbusplugins.
    static bool setPluginInstalled(bool installed);

public Q_SLOTS:
    RemoteActions Actions();
    RemoteMatches Match(const QString &query);
    void Run(const QString &matchId, const QString &actionId);

private:
    AppController *m_app;
};

} // namespace tumbler

Q_DECLARE_METATYPE(tumbler::RemoteMatch)
Q_DECLARE_METATYPE(tumbler::RemoteAction)
Q_DECLARE_METATYPE(tumbler::RemoteMatches)
Q_DECLARE_METATYPE(tumbler::RemoteActions)
