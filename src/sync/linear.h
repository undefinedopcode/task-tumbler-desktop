#pragma once

#include "sync.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <functional>

namespace tumbler::sync {

// Which Linear issues come in: assigned to you, in these state types,
// optionally only these teams.
struct LinearFilter {
    QStringList teamKeys; // empty: every team
    QStringList stateTypes{QStringLiteral("unstarted"), QStringLiteral("started")};
    int limit = 250;
};

// Linear's GraphQL API with a personal API key.
class LinearClient : public QObject
{
    Q_OBJECT
public:
    explicit LinearClient(QNetworkAccessManager *network, QObject *parent = nullptr);

    void setApiKey(const QString &key) { m_key = key; }
    void setEndpoint(const QUrl &url) { m_endpoint = url; }

    // authFailed: the key was refused (revoked, mistyped).
    using Done = std::function<void(const QJsonObject &data, const QString &error, bool authFailed)>;
    void query(const QString &graphql, const QJsonObject &variables, Done done);

    // Who the key belongs to and their teams: [{key, name}].
    void viewer(std::function<void(const QString &name, const QVariantList &teams, const QString &error, bool authFailed)> done);
    void activeIssues(const LinearFilter &filter,
                      std::function<void(const QList<RemoteTask> &issues, const QString &error, bool authFailed)> done);
    // For issues no longer in activeIssues(): done, cancelled, or just not
    // yours now, and for the done ones, when they were started and completed.
    void whatBecameOf(const QStringList &ids, const LinearFilter &filter,
                      std::function<void(const QHash<QString, Gone> &gone, const QHash<QString, Worked> &worked, const QString &error)> done);
    // Moves the issue to its team's first "completed" state.
    void complete(const QString &id, std::function<void(const QString &error)> done);

    static RemoteTask toTask(const QJsonObject &issue);

private:
    void page(const LinearFilter &filter, const QString &after, std::shared_ptr<QList<RemoteTask>> acc,
              std::function<void(const QList<RemoteTask> &, const QString &, bool)> done);

    QNetworkAccessManager *m_network;
    QString m_key;
    QUrl m_endpoint{QStringLiteral("https://api.linear.app/graphql")};
};

} // namespace tumbler::sync
