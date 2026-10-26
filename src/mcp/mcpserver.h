#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <optional>

namespace tumbler::mcp {

// Where tool calls go. In the app this is the running instance's external
// API (socketbackend.h), so agents' changes land on the same undo stack and show
// up live; in tests it's a fake.
class Backend
{
public:
    virtual ~Backend() = default;
    // `method` is a method of the app's ExternalApi (externalapi.h).
    virtual QVariant call(const QString &method, const QVariantList &args, QString *error) = 0;
};

// A Model Context Protocol server: JSON-RPC 2.0 messages in, replies out.
// Exposes tools to read the day and tasks, to capture, finish, start and
// schedule tasks, for notes and stats, and to close out the day and review
// the week.
class Server
{
public:
    explicit Server(Backend *backend);

    // The reply to a message, or nothing for notifications.
    std::optional<QJsonObject> handle(const QJsonObject &message);
    // The reply to one line of input, including parse errors.
    std::optional<QJsonObject> handleLine(const QByteArray &line);

    static QJsonArray tools();
    static QStringList supportedVersions();

private:
    QJsonObject callTool(const QString &name, const QJsonObject &args);
    Backend *m_backend;
};

// Reads newline-delimited JSON-RPC from stdin and answers on stdout until EOF.
int runStdio(Server &server);

} // namespace tumbler::mcp
