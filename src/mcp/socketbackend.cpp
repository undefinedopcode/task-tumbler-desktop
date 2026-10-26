#include "socketbackend.h"

#include "ipc.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QProcess>
#include <QThread>

namespace tumbler::mcp {

struct SocketBackend::Private {
    explicit Private(const QString &path)
        : client(path)
    {
    }
    ipc::Client client;
};

SocketBackend::SocketBackend(const QString &dbPath)
    : m_dbPath(dbPath)
    , d(std::make_unique<Private>(ipc::socketPath(dbPath)))
{
}

SocketBackend::~SocketBackend() = default;

bool SocketBackend::ensureRunning(QString *error)
{
    if (d->client.connect(200))
        return true;
    QStringList args{QStringLiteral("--hidden")};
    if (!m_dbPath.isEmpty())
        args << QStringLiteral("--db") << m_dbPath;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), args)) {
        *error = QStringLiteral("Task Tumbler isn't running and couldn't be started");
        return false;
    }
    QDeadlineTimer deadline(15000);
    while (!deadline.hasExpired()) {
        if (d->client.connect(200))
            return true;
        QThread::msleep(200);
    }
    *error = QStringLiteral("Task Tumbler didn't start in time");
    return false;
}

QVariant SocketBackend::call(const QString &method, const QVariantList &args, QString *error)
{
    if (!ensureRunning(error))
        return {};
    return d->client.call(method, args, error);
}

} // namespace tumbler::mcp
