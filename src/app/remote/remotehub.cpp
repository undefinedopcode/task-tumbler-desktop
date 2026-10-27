#include "remotehub.h"

#include "appcontroller.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>

namespace tumbler {

namespace {

RemoteHub *s_instance = nullptr;
constexpr int kSeenCaptures = 500;

QString newToken()
{
    QByteArray bytes(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), 8);
    return QString::fromLatin1(remote::toBase64Url(bytes));
}

QJsonObject json(const QVariantMap &m)
{
    return QJsonObject::fromVariantMap(m);
}

} // namespace

RemoteHub::RemoteHub(AppController *app, const QString &secretsPath, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_secretsPath(secretsPath)
    , m_server(this)
{
    loadSecrets();
    m_server.setToken(m_token);
    m_sender = std::make_unique<remote::PushSender>(&m_network, m_vapid, QStringLiteral("https://github.com/undefinedopcode") /* VAPID contact; push services see it */);
    connect(&m_server, &remote::HttpServer::streamsChanged, this, &RemoteHub::stateChanged);

    // Tell phones about changes, a burst at a time.
    m_changed.setSingleShot(true);
    m_changed.setInterval(300);
    connect(&m_changed, &QTimer::timeout, this, [this] { m_server.broadcast({{QStringLiteral("type"), QStringLiteral("changed")}}); });
    connect(app, &AppController::changed, &m_changed, qOverload<>(&QTimer::start));
    connect(app, &AppController::workingChanged, &m_changed, qOverload<>(&QTimer::start));
    connect(app, &AppController::settingsChanged, this, &RemoteHub::configure);
    configure();
}

RemoteHub::~RemoteHub() = default;

void RemoteHub::setInstance(RemoteHub *hub)
{
    s_instance = hub;
}

QString RemoteHub::defaultSecretsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/task-tumbler/remote.json");
}

quint16 RemoteHub::port() const
{
    return m_server.port();
}

// ---- secrets ---------------------------------------------------------------------

void RemoteHub::loadSecrets()
{
    QFile f(m_secretsPath);
    QJsonObject o;
    if (f.open(QIODevice::ReadOnly))
        o = QJsonDocument::fromJson(f.readAll()).object();
    m_token = o.value(QLatin1String("token")).toString();
    m_vapid.privateKey = remote::fromBase64Url(o.value(QLatin1String("vapidPrivate")).toString().toLatin1());
    m_vapid.publicKey = remote::fromBase64Url(o.value(QLatin1String("vapidPublic")).toString().toLatin1());
    for (const QJsonValue &v : o.value(QLatin1String("subscriptions")).toArray()) {
        if (const auto s = remote::Subscription::fromJson(v.toObject()))
            m_subscriptions.append(*s);
    }
    for (const QJsonValue &v : o.value(QLatin1String("seenCaptures")).toArray())
        m_seenCaptures.append(v.toString());

    bool changed = false;
    if (m_token.size() < 32) {
        m_token = newToken();
        changed = true;
    }
    if (!m_vapid.isValid()) {
        m_vapid = remote::VapidKeys::generate();
        changed = true;
    }
    if (changed)
        saveSecrets();
}

void RemoteHub::saveSecrets()
{
    QJsonArray subs;
    for (const remote::Subscription &s : std::as_const(m_subscriptions))
        subs.append(s.toJson());
    const QJsonObject o{
        {QStringLiteral("token"), m_token},
        {QStringLiteral("vapidPrivate"), QString::fromLatin1(remote::toBase64Url(m_vapid.privateKey))},
        {QStringLiteral("vapidPublic"), QString::fromLatin1(remote::toBase64Url(m_vapid.publicKey))},
        {QStringLiteral("subscriptions"), subs},
        {QStringLiteral("seenCaptures"), QJsonArray::fromStringList(m_seenCaptures)},
    };
    QDir().mkpath(QFileInfo(m_secretsPath).absolutePath());
    // Write then rename, readable by the owner only.
    const QString tmp = m_secretsPath + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
    QFile::remove(m_secretsPath);
    QFile::rename(tmp, m_secretsPath);
}

// ---- configuration -----------------------------------------------------------------

void RemoteHub::configure()
{
    const Queries q = m_app->queries();
    const bool enabled = q.setting(QStringLiteral("remote_enabled"), false).toBool();
    const QString listen = q.setting(QStringLiteral("remote_listen"), QStringLiteral("127.0.0.1:7789")).toString();
    const bool insecure = q.setting(QStringLiteral("remote_allow_insecure"), false).toBool();
    const QString wanted = enabled ? listen + (insecure ? QStringLiteral("!") : QString()) : QString();
    if (wanted == m_boundTo && (m_server.isListening() == enabled))
        return;

    m_server.close();
    m_error.clear();
    m_boundTo.clear();
    if (enabled) {
        const qsizetype colon = listen.lastIndexOf(QLatin1Char(':'));
        const QHostAddress address(listen.left(colon).remove(QLatin1Char('[')).remove(QLatin1Char(']')));
        const quint16 port = quint16(listen.mid(colon + 1).toUInt());
        QString error;
        if (colon < 0 || address.isNull() || port == 0)
            m_error = QStringLiteral("“%1” isn't an address:port").arg(listen);
        else if (!m_server.listen(address, port, insecure, &error))
            m_error = error;
        else
            m_boundTo = wanted;
    }
    Q_EMIT stateChanged();
}

QString RemoteHub::origin() const
{
    const QString publicUrl = m_app->queries().setting(QStringLiteral("remote_public_url"), QString()).toString().trimmed();
    if (!publicUrl.isEmpty())
        return publicUrl.endsWith(QLatin1Char('/')) ? publicUrl.chopped(1) : publicUrl;
    return QStringLiteral("http://") + m_app->queries().setting(QStringLiteral("remote_listen"), QStringLiteral("127.0.0.1:7789")).toString();
}

QVariantMap RemoteHub::state() const
{
    const Queries q = m_app->queries();
    const QString listen = q.setting(QStringLiteral("remote_listen"), QStringLiteral("127.0.0.1:7789")).toString();
    const QString port = listen.mid(listen.lastIndexOf(QLatin1Char(':')) + 1);
    QVariantList phones;
    for (const remote::Subscription &s : m_subscriptions) {
        phones.append(QVariantMap{{QStringLiteral("endpoint"), s.endpoint},
                                  {QStringLiteral("label"), s.label.isEmpty() ? QStringLiteral("Phone") : s.label},
                                  {QStringLiteral("addedAt"), s.addedAt}});
    }
    return {
        {QStringLiteral("enabled"), q.setting(QStringLiteral("remote_enabled"), false).toBool()},
        {QStringLiteral("running"), m_server.isListening()},
        {QStringLiteral("error"), m_error},
        {QStringLiteral("listen"), listen},
        {QStringLiteral("publicUrl"), q.setting(QStringLiteral("remote_public_url"), QString()).toString()},
        // The token travels in the fragment: browsers never send that part to a server.
        {QStringLiteral("pairUrl"), origin() + QStringLiteral("/#pair=") + m_token},
        {QStringLiteral("secure"), origin().startsWith(QLatin1String("https://"))},
        {QStringLiteral("phones"), phones},
        {QStringLiteral("streams"), m_server.streamCount()},
        {QStringLiteral("serveCommand"), QStringLiteral("tailscale serve --bg --https=%1 http://%2").arg(port, listen)},
        {QStringLiteral("notify"), q.setting(QStringLiteral("remote_notify"), QStringLiteral("away")).toString()},
        {QStringLiteral("tailscaleName"), m_tailscaleName},
    };
}

void RemoteHub::setEnabled(bool enabled)
{
    m_app->setSetting(QStringLiteral("remote_enabled"), enabled);
}

void RemoteHub::setPublicUrl(const QString &url)
{
    m_app->setSetting(QStringLiteral("remote_public_url"), url.trimmed());
    Q_EMIT stateChanged();
}

void RemoteHub::rotateToken()
{
    m_token = newToken();
    m_subscriptions.clear(); // their owners must pair again
    m_server.setToken(m_token);
    saveSecrets();
    Q_EMIT stateChanged();
    Q_EMIT m_app->message(QStringLiteral("New pairing code: phones need to scan it again"));
}

void RemoteHub::forgetPhone(const QString &endpoint)
{
    if (removeSubscription(endpoint))
        Q_EMIT m_app->message(QStringLiteral("That phone won't get notifications any more"));
}

void RemoteHub::detectTailscale()
{
    // Read-only: asks for this machine's tailnet name. Never changes serve config.
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc](int code) {
        proc->deleteLater();
        const QJsonObject status = QJsonDocument::fromJson(proc->readAllStandardOutput()).object();
        QString name = status.value(QLatin1String("Self")).toObject().value(QLatin1String("DNSName")).toString();
        if (name.endsWith(QLatin1Char('.')))
            name.chop(1);
        if (code != 0 || name.isEmpty()) {
            Q_EMIT m_app->message(QStringLiteral("Couldn't ask Tailscale for this machine's name (is it running?)"));
            return;
        }
        m_tailscaleName = name;
        const QString listen = m_app->queries().setting(QStringLiteral("remote_listen"), QStringLiteral("127.0.0.1:7789")).toString();
        setPublicUrl(QStringLiteral("https://%1:%2").arg(name, listen.mid(listen.lastIndexOf(QLatin1Char(':')) + 1)));
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        proc->deleteLater();
        Q_EMIT m_app->message(QStringLiteral("Tailscale isn't installed"));
    });
    // On a Mac the app store build keeps its CLI inside the app.
    QString tailscale = QStandardPaths::findExecutable(QStringLiteral("tailscale"));
    if (tailscale.isEmpty() && QFile::exists(QStringLiteral("/Applications/Tailscale.app/Contents/MacOS/Tailscale")))
        tailscale = QStringLiteral("/Applications/Tailscale.app/Contents/MacOS/Tailscale");
    proc->start(tailscale.isEmpty() ? QStringLiteral("tailscale") : tailscale, {QStringLiteral("status"), QStringLiteral("--json")});
}

void RemoteHub::sendTest()
{
    if (m_subscriptions.isEmpty()) {
        Q_EMIT m_app->message(QStringLiteral("No phone has turned on notifications yet"));
        return;
    }
    push({{QStringLiteral("title"), QStringLiteral("Task Tumbler")},
          {QStringLiteral("body"), QStringLiteral("Notifications from your computer reach this phone.")},
          {QStringLiteral("tag"), QStringLiteral("tt-test")}});
    Q_EMIT m_app->message(QStringLiteral("Test notification sent"));
}

void RemoteHub::copyToClipboard(const QString &text)
{
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        app->clipboard()->setText(text);
}

// ---- push --------------------------------------------------------------------------

QString RemoteHub::route(const QString &mode, bool phonePaired, bool userAway)
{
    if (!phonePaired || mode == QLatin1String("desktop"))
        return QStringLiteral("desktop");
    if (mode == QLatin1String("both"))
        return QStringLiteral("both");
    return userAway ? QStringLiteral("phone") : QStringLiteral("desktop");
}

void RemoteHub::push(const QJsonObject &payload, const QString &urgency)
{
    const QList<remote::Subscription> subs = m_subscriptions;
    for (const remote::Subscription &s : subs) {
        const QString endpoint = s.endpoint;
        m_sender->send(s, payload, 15 * 60, urgency, [this, endpoint](int status, const QString &error) {
            // Gone: the phone unsubscribed or reinstalled. Forget it.
            if (status == 404 || status == 410)
                removeSubscription(endpoint);
            else if (!error.isEmpty())
                qWarning() << "push failed:" << status << error;
        });
    }
}

// ---- remote::Backend ------------------------------------------------------------------

QJsonObject RemoteHub::snapshot()
{
    return {
        {QStringLiteral("today"), json(m_app->today())},
        {QStringLiteral("working"), json(m_app->working())},
        {QStringLiteral("energy"), json(m_app->energy())},
        {QStringLiteral("serverTime"), m_app->now()},
    };
}

QJsonObject RemoteHub::closeOut()
{
    return json(m_app->closeOut());
}

QJsonObject RemoteHub::command(const QJsonObject &c, QString *error)
{
    const QString type = c.value(QLatin1String("type")).toString();
    const QString id = c.value(QLatin1String("id")).toString();
    const QString taskId = c.value(QLatin1String("taskId")).toString();
    bool ok = false;

    if (type == QLatin1String("capture")) {
        // The phone retries captures from its outbox; the id makes that safe.
        const QString clientId = c.value(QLatin1String("clientId")).toString();
        if (!clientId.isEmpty() && m_seenCaptures.contains(clientId))
            return {{QStringLiteral("ok"), true}, {QStringLiteral("duplicate"), true}};
        const QString newId = m_app->capture(c.value(QLatin1String("text")).toString().left(1000));
        ok = !newId.isEmpty();
        if (ok && !clientId.isEmpty()) {
            m_seenCaptures.append(clientId);
            while (m_seenCaptures.size() > kSeenCaptures)
                m_seenCaptures.removeFirst();
            saveSecrets();
        }
        if (ok)
            return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), newId}};
    } else if (type == QLatin1String("completeSchedule")) {
        ok = m_app->completeSchedule(id);
    } else if (type == QLatin1String("bump")) {
        ok = m_app->bump(id, std::clamp(c.value(QLatin1String("minutes")).toInt(15), 5, 24 * 60));
    } else if (type == QLatin1String("bumpNext")) {
        ok = m_app->bumpToNextSlot(id);
    } else if (type == QLatin1String("startWork")) {
        ok = m_app->startWork(taskId);
    } else if (type == QLatin1String("stopWork")) {
        ok = m_app->stopWork();
    } else if (type == QLatin1String("occurrence")) {
        ok = m_app->setOccurrence(id, c.value(QLatin1String("status")).toString());
    } else if (type == QLatin1String("completeTask")) {
        ok = m_app->setTaskStatus(taskId, QStringLiteral("completed"));
    } else if (type == QLatin1String("scheduleNext")) {
        ok = m_app->scheduleNextSlot(taskId);
    } else if (type == QLatin1String("leftover")) {
        ok = m_app->resolveLeftover(c.value(QLatin1String("kind")).toString(), id, c.value(QLatin1String("choice")).toString());
    } else if (type == QLatin1String("clearLeftovers")) {
        ok = m_app->clearLeftovers();
    } else if (type == QLatin1String("dayNote")) {
        // Saved as typed; an empty note before there is one is nothing to save.
        const QString text = c.value(QLatin1String("text")).toString().left(10000);
        ok = !m_app->saveDayNote(text).isEmpty() || text.trimmed().isEmpty();
    } else if (type == QLatin1String("finishCloseOut")) {
        m_app->finishCloseOut(c.value(QLatin1String("quietly")).toBool());
        ok = true;
    } else if (type == QLatin1String("energy")) {
        ok = m_app->logEnergy(std::clamp(c.value(QLatin1String("level")).toInt(), 1, 10));
    } else {
        *error = QStringLiteral("unknown command “%1”").arg(type);
        return {};
    }
    if (!ok)
        *error = QStringLiteral("that didn't work (it may already be done)");
    return {{QStringLiteral("ok"), ok}};
}

QJsonObject RemoteHub::preview(const QString &text)
{
    return json(m_app->previewCapture(text));
}

bool RemoteHub::addSubscription(const remote::Subscription &subscription)
{
    remote::Subscription s = subscription;
    s.addedAt = m_app->now();
    m_subscriptions.removeIf([&](const remote::Subscription &e) { return e.endpoint == s.endpoint; });
    m_subscriptions.append(s);
    saveSecrets();
    Q_EMIT stateChanged();
    return true;
}

bool RemoteHub::removeSubscription(const QString &endpoint)
{
    const bool removed = m_subscriptions.removeIf([&](const remote::Subscription &e) { return e.endpoint == endpoint; }) > 0;
    if (removed) {
        saveSecrets();
        Q_EMIT stateChanged();
    }
    return removed;
}

} // namespace tumbler
