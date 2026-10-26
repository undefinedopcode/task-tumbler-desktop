// AppController: launchers, focus mode and time away from the computer.
// Execution lives in desktop/launcher.cpp; this file decides what to run and
// whether a command may run.

#include "appcontroller.h"
#include "desktop/apps.h"
#include "operations.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QUrl>

namespace tumbler {

namespace {
constexpr qint64 kMinute = 60 * 1000;
const QString kApproved = QStringLiteral("approved_commands");

QStringList argvOf(const QString &json)
{
    QStringList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json.toUtf8()).array())
        out.append(v.toString());
    return out;
}
} // namespace

QStringList AppController::splitCommand(const QString &line)
{
    // Words separated by spaces; "double" or 'single' quotes keep spaces
    // together; backslash escapes the next character. No globbing, no
    // variables, no pipes: what you see is exactly what runs.
    QStringList words;
    QString current;
    bool inWord = false;
    QChar quote;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            else if (c == QLatin1Char('\\') && quote == QLatin1Char('"') && i + 1 < line.size())
                current += line.at(++i);
            else
                current += c;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            inWord = true;
        } else if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            current += line.at(++i);
            inWord = true;
        } else if (c.isSpace()) {
            if (inWord)
                words.append(current);
            current.clear();
            inWord = false;
        } else {
            current += c;
            inWord = true;
        }
    }
    if (inWord)
        words.append(current);
    return words;
}

QString AppController::commandHash(const QString &argvJson)
{
    return QString::fromLatin1(QCryptographicHash::hash(argvJson.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantMap AppController::actionToVariant(const TaskAction &a) const
{
    QString label = a.target;
    QString icon;
    bool needsApproval = false;
    switch (a.kind) {
    case TaskAction::Kind::Url:
        icon = QStringLiteral("internet-web-browser");
        label = QUrl(a.target).host().isEmpty() ? a.target : QUrl(a.target).host() + QUrl(a.target).path();
        break;
    case TaskAction::Kind::File:
        icon = QStringLiteral("document-open");
        label = QFileInfo(a.target).fileName();
        break;
    case TaskAction::Kind::App: {
        const auto app = apps::find(a.target);
        icon = app ? app->icon : QStringLiteral("application-x-executable");
        label = app ? app->name : a.target;
        break;
    }
    case TaskAction::Kind::Command: {
        icon = QStringLiteral("utilities-terminal");
        const QStringList argv = argvOf(a.target);
        QStringList shown;
        for (const QString &w : argv)
            shown.append(w.contains(QLatin1Char(' ')) ? QLatin1Char('"') + w + QLatin1Char('"') : w);
        label = shown.join(QLatin1Char(' '));
        const QJsonArray approved = queries().setting(kApproved, QJsonArray()).toArray();
        needsApproval = !approved.contains(commandHash(a.target));
        break;
    }
    }
    return {
        {QStringLiteral("id"), a.id},
        {QStringLiteral("kind"), nameOf(a.kind)},
        {QStringLiteral("target"), a.target},
        {QStringLiteral("label"), label},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("onStart"), a.onStart},
        {QStringLiteral("needsApproval"), needsApproval},
    };
}

QString AppController::addAction(const QString &taskId, const QString &kind, const QString &target, bool onStart)
{
    const auto k = actionKindFromString(kind);
    if (!k)
        return {};
    TaskAction a;
    a.id = newId();
    a.taskId = taskId;
    a.kind = *k;
    a.onStart = onStart;
    a.target = target.trimmed();
    if (a.kind == TaskAction::Kind::File && a.target.startsWith(QLatin1String("file:")))
        a.target = QUrl(a.target).toLocalFile();
    if (a.kind == TaskAction::Kind::Url && !a.target.contains(QLatin1String("://")) && !a.target.isEmpty())
        a.target.prepend(QStringLiteral("https://"));
    if (a.kind == TaskAction::Kind::Command)
        a.target = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(splitCommand(target))).toJson(QJsonDocument::Compact));
    QString error;
    return apply(ops::addAction(queries(), a, now(), &error), error) ? a.id : QString();
}

bool AppController::removeAction(const QString &id)
{
    return apply(ops::removeAction(id));
}

void AppController::runAction(const QString &id)
{
    if (const auto a = queries().action(id))
        launch(*a, true);
}

void AppController::approveCommand(const QString &actionId)
{
    const auto a = queries().action(actionId);
    if (!a || a->kind != TaskAction::Kind::Command)
        return;
    QJsonArray approved = queries().setting(kApproved, QJsonArray()).toArray();
    const QString hash = commandHash(a->target);
    if (!approved.contains(hash)) {
        approved.append(hash);
        apply(ops::setSetting(kApproved, approved));
    }
    launch(*a, false);
}

void AppController::launch(const TaskAction &a, bool askIfNeeded)
{
    const QVariantMap v = actionToVariant(a);
    if (a.kind == TaskAction::Kind::Command && v.value(QStringLiteral("needsApproval")).toBool()) {
        // A command runs only once the user has seen exactly what it is.
        // Changing it in any way asks again (the approval is a hash).
        if (askIfNeeded) {
            const auto task = queries().task(a.taskId);
            Q_EMIT commandNeedsApproval(a.id, task ? task->title : QString(), v.value(QStringLiteral("label")).toString());
        }
        return;
    }
    QVariantMap request = v;
    request.insert(QStringLiteral("argv"), argvOf(a.target));
    Q_EMIT launchRequested(request);
}

void AppController::launchForTask(const QString &taskId)
{
    for (const TaskAction &a : queries().actions(taskId)) {
        if (a.onStart)
            launch(a, true);
    }
}

QVariantList AppController::installedApps() const
{
    QVariantList out;
    for (const apps::App &a : apps::installed())
        out.append(QVariantMap{{QStringLiteral("id"), a.id}, {QStringLiteral("name"), a.name}, {QStringLiteral("icon"), a.icon}});
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(),
                                                                         Qt::CaseInsensitive)
            < 0;
    });
    return out;
}

void AppController::setFocusMode(bool on)
{
    if (on == m_focusMode)
        return;
    m_focusMode = on;
    Q_EMIT focusModeChanged();
}

void AppController::startFocus(const QString &taskId)
{
    if (working().value(QStringLiteral("taskId")).toString() != taskId && !startWork(taskId))
        return;
    setFocusMode(true);
}

void AppController::reportAway(qint64 since, qint64 until)
{
    // Only matters while a session is running, and only if it started
    // before they left.
    const auto session = queries().activeWorkLog();
    if (!session || session->startedAt >= since)
        return;
    const auto task = queries().task(session->taskId);
    m_away = QVariantMap{
        {QStringLiteral("since"), since},
        {QStringLiteral("until"), until},
        {QStringLiteral("minutes"), int((until - since) / kMinute)},
        {QStringLiteral("title"), task ? task->title : QString()},
        {QStringLiteral("taskId"), session->taskId},
        {QStringLiteral("sinceLabel"), QLocale().toString(QDateTime::fromMSecsSinceEpoch(since, zone()).time(), QLocale::ShortFormat)},
    };
    Q_EMIT awayChanged();
}

void AppController::resolveAway(const QString &choice)
{
    if (m_away.isEmpty())
        return;
    const qint64 since = m_away.value(QStringLiteral("since")).toLongLong();
    const QString taskId = m_away.value(QStringLiteral("taskId")).toString();
    m_away.clear();
    Q_EMIT awayChanged();

    if (choice == QLatin1String("keep"))
        return;
    QString error;
    // Only trim if that same session is still running.
    const auto session = queries().activeWorkLog();
    if (!session || session->taskId != taskId)
        return;
    if (!apply(ops::stopWorkAt(queries(), since, now(), &error), error))
        return;
    if (choice == QLatin1String("trimRestart")) {
        apply(ops::startWork(queries(), newId(), taskId, now(), &error), error);
        Q_EMIT message(QStringLiteral("Time away left out; timer restarted"));
    } else {
        Q_EMIT message(QStringLiteral("Stopped as of when you left"));
    }
}

} // namespace tumbler
