#include "ipc.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaMethod>
#include <QStandardPaths>
#include <array>

namespace tumbler::ipc {

namespace {

// Requests are small; anything longer than this without a newline isn't one.
constexpr qsizetype kMaxLine = 1 << 20;
// What QMetaMethod::invoke takes.
constexpr int kMaxArgs = 10;

QByteArray line(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray failure(const QString &error)
{
    return line({{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}});
}

} // namespace

QString socketPath(const QString &dbPath)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    QString name = QStringLiteral("task-tumbler");
    if (!dbPath.isEmpty()) {
        const QFileInfo info(dbPath);
        const QString full = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
        name += QLatin1Char('-') + QString::fromLatin1(QCryptographicHash::hash(full.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    }
    return dir + QLatin1Char('/') + name + QStringLiteral(".sock");
}

// ---- server ---------------------------------------------------------------------

Server::Server(QObject *target, std::function<QString()> takeError, QObject *parent)
    : QObject(parent)
    , m_target(target)
    , m_takeError(std::move(takeError))
    , m_server(new QLocalServer(this))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &Server::onConnection);
}

Server::~Server() = default;

bool Server::listen(const QString &path, QString *error)
{
    // Asked first: on Unix, listen() itself would quietly replace a live socket.
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(500)) {
        if (error)
            *error = QStringLiteral("another instance is listening on %1").arg(path);
        return false;
    }
    // Whatever's left is from an instance that didn't shut down cleanly.
    QLocalServer::removeServer(path);
    if (m_server->listen(path))
        return true;
    if (error)
        *error = m_server->errorString();
    return false;
}

QString Server::path() const
{
    return m_server->fullServerName();
}

void Server::onConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            while (socket->canReadLine())
                socket->write(handle(socket->readLine()));
            if (socket->bytesAvailable() > kMaxLine)
                socket->abort();
        });
    }
}

QByteArray Server::handle(const QByteArray &request)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(request, &parseError);
    if (!doc.isObject())
        return failure(QStringLiteral("not a JSON object: %1").arg(parseError.errorString()));
    const QString method = doc.object().value(QLatin1String("method")).toString();
    const QJsonArray args = doc.object().value(QLatin1String("args")).toArray();

    if (method == QLatin1String("Activate")) {
        QStringList arguments;
        for (const QJsonValue &a : args)
            arguments.append(a.toString());
        Q_EMIT activateRequested(arguments);
        return line({{QStringLiteral("ok"), true}, {QStringLiteral("result"), true}});
    }

    const QMetaObject *mo = m_target->metaObject();
    QMetaMethod found;
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.methodType() == QMetaMethod::Slot && (m.attributes() & QMetaMethod::Scriptable) && m.name() == method.toLatin1()
            && m.parameterCount() == args.size()) {
            found = m;
            break;
        }
    }
    if (!found.isValid() || args.size() > kMaxArgs)
        return failure(QStringLiteral("no method %1 taking %2 arguments").arg(method).arg(args.size()));

    std::array<QVariant, kMaxArgs> values;
    std::array<QGenericArgument, kMaxArgs> generic;
    for (int i = 0; i < found.parameterCount(); ++i) {
        const QMetaType type = found.parameterMetaType(i);
        QVariant v = args.at(i).toVariant();
        if (v.isNull())
            v = QVariant(type);
        else if (!v.convert(type))
            return failure(QStringLiteral("argument %1 of %2 should be a %3").arg(i + 1).arg(method, QLatin1String(type.name())));
        values[i] = v;
        generic[i] = QGenericArgument(type.name(), values[i].constData());
    }
    if (m_takeError)
        m_takeError(); // one left over from a D-Bus call isn't this call's
    QVariant result;
    QGenericReturnArgument ret;
    if (found.returnMetaType().id() != QMetaType::Void) {
        result = QVariant(found.returnMetaType());
        ret = QGenericReturnArgument(found.typeName(), result.data());
    }
    if (!found.invoke(m_target, Qt::DirectConnection, ret, generic[0], generic[1], generic[2], generic[3], generic[4], generic[5],
                      generic[6], generic[7], generic[8], generic[9]))
        return failure(QStringLiteral("couldn't call %1").arg(method));

    const QString error = m_takeError ? m_takeError() : QString();
    if (!error.isEmpty())
        return failure(error);
    return line({{QStringLiteral("ok"), true}, {QStringLiteral("result"), QJsonValue::fromVariant(result)}});
}

// ---- client ---------------------------------------------------------------------

Client::Client(const QString &path)
    : m_path(path)
    , m_socket(new QLocalSocket)
{
}

Client::~Client()
{
    delete m_socket;
}

bool Client::connect(int timeoutMs)
{
    if (m_socket->state() == QLocalSocket::ConnectedState)
        return true;
    m_socket->connectToServer(m_path);
    return m_socket->waitForConnected(timeoutMs);
}

QVariant Client::call(const QString &method, const QVariantList &args, QString *error, int timeoutMs)
{
    if (!connect()) {
        *error = QStringLiteral("Task Tumbler isn't running");
        return {};
    }
    m_socket->write(line({{QStringLiteral("method"), method}, {QStringLiteral("args"), QJsonArray::fromVariantList(args)}}));
    QDeadlineTimer deadline(timeoutMs);
    m_socket->waitForBytesWritten(timeoutMs);
    while (!m_socket->canReadLine()) {
        if (deadline.hasExpired() || !m_socket->waitForReadyRead(int(deadline.remainingTime()))) {
            *error = m_socket->state() == QLocalSocket::ConnectedState ? QStringLiteral("Task Tumbler didn't answer in time")
                                                                       : QStringLiteral("Task Tumbler closed the connection");
            m_socket->abort();
            return {};
        }
    }
    const QJsonObject reply = QJsonDocument::fromJson(m_socket->readLine()).object();
    if (!reply.value(QLatin1String("ok")).toBool()) {
        *error = reply.value(QLatin1String("error")).toString(QStringLiteral("Task Tumbler sent a reply it couldn't read"));
        return {};
    }
    return reply.value(QLatin1String("result")).toVariant();
}

} // namespace tumbler::ipc
