#include "linear.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimeZone>

namespace tumbler::sync {

namespace {

constexpr int kTimeoutMs = 30000;

const QString kIssueFields = QStringLiteral(
    "id identifier title description priority dueDate url state { type } team { key } labels { nodes { name } }");

QJsonObject issueFilter(const LinearFilter &f)
{
    QJsonObject filter{
        {QStringLiteral("assignee"), QJsonObject{{QStringLiteral("isMe"), QJsonObject{{QStringLiteral("eq"), true}}}}},
        {QStringLiteral("state"),
         QJsonObject{{QStringLiteral("type"), QJsonObject{{QStringLiteral("in"), QJsonArray::fromStringList(f.stateTypes)}}}}},
    };
    if (!f.teamKeys.isEmpty()) {
        filter.insert(QStringLiteral("team"),
                      QJsonObject{{QStringLiteral("key"), QJsonObject{{QStringLiteral("in"), QJsonArray::fromStringList(f.teamKeys)}}}});
    }
    return filter;
}

// Linear's timestamps ("2026-11-05T01:40:00.000Z"); 0 when absent.
qint64 msOf(const QJsonValue &v)
{
    const QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    return t.isValid() ? t.toMSecsSinceEpoch() : 0;
}

} // namespace

LinearClient::LinearClient(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

void LinearClient::query(const QString &graphql, const QJsonObject &variables, Done done)
{
    if (m_key.isEmpty()) {
        done({}, QStringLiteral("no Linear API key"), true);
        return;
    }
    QNetworkRequest req(m_endpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Personal API keys go in as they are, without "Bearer".
    req.setRawHeader("Authorization", m_key.toUtf8());
    req.setTransferTimeout(kTimeoutMs);
    const QJsonObject body{{QStringLiteral("query"), graphql}, {QStringLiteral("variables"), variables}};
    QNetworkReply *reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonArray errors = json.value(QLatin1String("errors")).toArray();
        if (!errors.isEmpty() || status == 401 || status == 403) {
            const QJsonObject first = errors.first().toObject();
            const QString code = first.value(QLatin1String("extensions")).toObject().value(QLatin1String("code")).toString();
            const bool auth = status == 401 || status == 403 || code == QLatin1String("AUTHENTICATION_ERROR");
            done({}, auth ? QStringLiteral("Linear didn't accept the API key")
                          : QStringLiteral("Linear: %1").arg(first.value(QLatin1String("message")).toString(QStringLiteral("HTTP %1").arg(status))),
                 auth);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            done({}, QStringLiteral("couldn't reach Linear: %1").arg(reply->errorString()), false);
            return;
        }
        done(json.value(QLatin1String("data")).toObject(), {}, false);
    });
}

void LinearClient::viewer(std::function<void(const QString &, const QVariantList &, const QString &, bool)> done)
{
    query(QStringLiteral("query { viewer { name } teams(first: 100) { nodes { key name } } }"), {},
          [done](const QJsonObject &data, const QString &error, bool auth) {
              QVariantList teams;
              for (const QJsonValue &t : data.value(QLatin1String("teams")).toObject().value(QLatin1String("nodes")).toArray())
                  teams.append(t.toObject().toVariantMap());
              done(data.value(QLatin1String("viewer")).toObject().value(QLatin1String("name")).toString(), teams, error, auth);
          });
}

RemoteTask LinearClient::toTask(const QJsonObject &issue)
{
    RemoteTask r;
    r.externalId = issue.value(QLatin1String("id")).toString();
    r.title = issue.value(QLatin1String("title")).toString();
    r.description = issue.value(QLatin1String("description")).toString().left(4000);
    // Linear: 0 none, 1 urgent, 2 high, 3 normal, 4 low.
    switch (issue.value(QLatin1String("priority")).toInt()) {
    case 1: r.priority = Priority::Urgent; break;
    case 2: r.priority = Priority::High; break;
    case 4: r.priority = Priority::Low; break;
    default: r.priority = Priority::Medium; break;
    }
    r.dueDate = QDate::fromString(issue.value(QLatin1String("dueDate")).toString(), Qt::ISODate);
    r.url = issue.value(QLatin1String("url")).toString();
    r.tags = {QStringLiteral("linear")};
    const QString team = issue.value(QLatin1String("team")).toObject().value(QLatin1String("key")).toString();
    if (!team.isEmpty())
        r.tags.append(QStringLiteral("linear:") + team);
    for (const QJsonValue &l : issue.value(QLatin1String("labels")).toObject().value(QLatin1String("nodes")).toArray()) {
        const QString name = l.toObject().value(QLatin1String("name")).toString().trimmed();
        if (!name.isEmpty() && !r.tags.contains(name, Qt::CaseInsensitive))
            r.tags.append(name);
    }
    const QString state = issue.value(QLatin1String("state")).toObject().value(QLatin1String("type")).toString();
    r.open = state != QLatin1String("completed") && state != QLatin1String("canceled");
    return r;
}

void LinearClient::activeIssues(const LinearFilter &filter, std::function<void(const QList<RemoteTask> &, const QString &, bool)> done)
{
    page(filter, {}, std::make_shared<QList<RemoteTask>>(), std::move(done));
}

void LinearClient::page(const LinearFilter &filter, const QString &after, std::shared_ptr<QList<RemoteTask>> acc,
                        std::function<void(const QList<RemoteTask> &, const QString &, bool)> done)
{
    const QString q = QStringLiteral("query Active($filter: IssueFilter, $after: String) { issues(filter: $filter, first: 100, after: $after, "
                                     "orderBy: updatedAt) { nodes { %1 } pageInfo { hasNextPage endCursor } } }")
                          .arg(kIssueFields);
    QJsonObject vars{{QStringLiteral("filter"), issueFilter(filter)}};
    if (!after.isEmpty())
        vars.insert(QStringLiteral("after"), after);
    query(q, vars, [this, filter, acc, done](const QJsonObject &data, const QString &error, bool auth) {
        if (!error.isEmpty()) {
            done({}, error, auth);
            return;
        }
        const QJsonObject issues = data.value(QLatin1String("issues")).toObject();
        for (const QJsonValue &n : issues.value(QLatin1String("nodes")).toArray()) {
            if (acc->size() < filter.limit)
                acc->append(toTask(n.toObject()));
        }
        const QJsonObject info = issues.value(QLatin1String("pageInfo")).toObject();
        if (info.value(QLatin1String("hasNextPage")).toBool() && acc->size() < filter.limit)
            page(filter, info.value(QLatin1String("endCursor")).toString(), acc, done);
        else
            done(*acc, {}, false);
    });
}

void LinearClient::whatBecameOf(const QStringList &ids, const LinearFilter &filter,
                                std::function<void(const QHash<QString, Gone> &, const QHash<QString, Worked> &, const QString &)> done)
{
    if (ids.isEmpty()) {
        done({}, {}, {});
        return;
    }
    const QStringList batch = ids.mid(0, 100); // the rest are asked about next sync
    query(QStringLiteral("query Check($ids: [ID!]) { issues(filter: { id: { in: $ids } }, includeArchived: true, first: 100) "
                         "{ nodes { id archivedAt startedAt completedAt state { type } assignee { isMe } team { key } } } }"),
          {{QStringLiteral("ids"), QJsonArray::fromStringList(batch)}},
          [batch, filter, done](const QJsonObject &data, const QString &error, bool) {
              QHash<QString, Gone> gone;
              QHash<QString, Worked> worked;
              if (!error.isEmpty()) {
                  done(gone, worked, error);
                  return;
              }
              for (const QString &id : batch)
                  gone.insert(id, Gone::Cancelled); // not returned at all: deleted
              for (const QJsonValue &v : data.value(QLatin1String("issues")).toObject().value(QLatin1String("nodes")).toArray()) {
                  const QJsonObject n = v.toObject();
                  const QString id = n.value(QLatin1String("id")).toString();
                  const QString state = n.value(QLatin1String("state")).toObject().value(QLatin1String("type")).toString();
                  const bool mine = n.value(QLatin1String("assignee")).toObject().value(QLatin1String("isMe")).toBool();
                  const QString team = n.value(QLatin1String("team")).toObject().value(QLatin1String("key")).toString();
                  if (state == QLatin1String("completed")) {
                      gone.insert(id, Gone::Completed);
                      worked.insert(id, {msOf(n.value(QLatin1String("startedAt"))), msOf(n.value(QLatin1String("completedAt")))});
                  }
                  else if (state == QLatin1String("canceled") || !n.value(QLatin1String("archivedAt")).isNull())
                      gone.insert(id, Gone::Cancelled);
                  else if (!mine || !filter.stateTypes.contains(state) || (!filter.teamKeys.isEmpty() && !filter.teamKeys.contains(team)))
                      gone.insert(id, Gone::Elsewhere);
                  else
                      gone.insert(id, Gone::Unknown); // still matches; it was past the limit
              }
              done(gone, worked, {});
          });
}

void LinearClient::completedSince(qint64 since, const LinearFilter &filter,
                                  std::function<void(const QList<FinishedRemote> &, const QString &)> done)
{
    QJsonObject f = issueFilter(filter);
    f.remove(QStringLiteral("state"));
    f.insert(QStringLiteral("completedAt"),
             QJsonObject{{QStringLiteral("gt"), QDateTime::fromMSecsSinceEpoch(since, QTimeZone::UTC).toString(Qt::ISODateWithMs)}});
    query(QStringLiteral("query Done($filter: IssueFilter) { issues(filter: $filter, first: 100, orderBy: updatedAt) "
                         "{ nodes { %1 startedAt completedAt } } }")
              .arg(kIssueFields),
          {{QStringLiteral("filter"), f}}, [done](const QJsonObject &data, const QString &error, bool) {
              QList<FinishedRemote> out;
              if (error.isEmpty()) {
                  for (const QJsonValue &v : data.value(QLatin1String("issues")).toObject().value(QLatin1String("nodes")).toArray()) {
                      const QJsonObject n = v.toObject();
                      if (n.value(QLatin1String("state")).toObject().value(QLatin1String("type")).toString() != QLatin1String("completed"))
                          continue; // reopened since
                      out.append({toTask(n), {msOf(n.value(QLatin1String("startedAt"))), msOf(n.value(QLatin1String("completedAt")))}});
                  }
              }
              done(out, error);
          });
}

void LinearClient::complete(const QString &id, std::function<void(const QString &)> done)
{
    query(QStringLiteral("query States($id: String!) { issue(id: $id) { state { type } team { states { nodes { id type position } } } } }"),
          {{QStringLiteral("id"), id}}, [this, id, done](const QJsonObject &data, const QString &error, bool) {
              if (!error.isEmpty()) {
                  done(error);
                  return;
              }
              const QJsonObject issue = data.value(QLatin1String("issue")).toObject();
              if (issue.isEmpty()) {
                  done({}); // deleted there: nothing to tell
                  return;
              }
              if (issue.value(QLatin1String("state")).toObject().value(QLatin1String("type")).toString() == QLatin1String("completed")) {
                  done({});
                  return;
              }
              QString stateId;
              double best = 0;
              for (const QJsonValue &v : issue.value(QLatin1String("team")).toObject().value(QLatin1String("states")).toObject().value(QLatin1String("nodes")).toArray()) {
                  const QJsonObject s = v.toObject();
                  if (s.value(QLatin1String("type")).toString() != QLatin1String("completed"))
                      continue;
                  const double position = s.value(QLatin1String("position")).toDouble();
                  if (stateId.isEmpty() || position < best) {
                      stateId = s.value(QLatin1String("id")).toString();
                      best = position;
                  }
              }
              if (stateId.isEmpty()) {
                  done(QStringLiteral("that team has no Done state"));
                  return;
              }
              query(QStringLiteral("mutation Done($id: String!, $stateId: String!) { issueUpdate(id: $id, input: { stateId: $stateId }) { success } }"),
                    {{QStringLiteral("id"), id}, {QStringLiteral("stateId"), stateId}},
                    [done](const QJsonObject &data, const QString &error, bool) {
                        if (!error.isEmpty())
                            done(error);
                        else if (!data.value(QLatin1String("issueUpdate")).toObject().value(QLatin1String("success")).toBool())
                            done(QStringLiteral("Linear didn't accept the change"));
                        else
                            done({});
                    });
          });
}

} // namespace tumbler::sync
