#include "database.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <map>

namespace tumbler {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// Embedded migrations by number, read from the resource directory.
std::map<int, QString> migrationFiles()
{
    static const QRegularExpression name(QStringLiteral(R"(^(\d+)_.*\.sql$)"));
    std::map<int, QString> files;
    const QDir dir(QStringLiteral(":/migrations"));
    for (const QString &entry : dir.entryList({QStringLiteral("*.sql")}, QDir::Files)) {
        const auto m = name.match(entry);
        if (m.hasMatch())
            files.emplace(m.captured(1).toInt(), dir.filePath(entry));
    }
    return files;
}

} // namespace

Database::Database(const QString &connectionName)
    : m_connectionName(connectionName)
{
}

Database::~Database()
{
    close();
}

QString Database::defaultPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/task-tumbler");
    return dir + QStringLiteral("/tumbler.db");
}

bool Database::open(const QString &path, QString *error)
{
    close();

    if (path != QLatin1String(":memory:")) {
        const QFileInfo info(path);
        if (!QDir().mkpath(info.absolutePath())) {
            setError(error, QStringLiteral("cannot create directory %1").arg(info.absolutePath()));
            return false;
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        setError(error, db.lastError().text());
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }
    m_path = path;

    const bool memory = path == QLatin1String(":memory:");
    const QStringList pragmas = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        memory ? QStringLiteral("PRAGMA journal_mode = MEMORY") : QStringLiteral("PRAGMA journal_mode = WAL"),
        QStringLiteral("PRAGMA synchronous = NORMAL"),
        QStringLiteral("PRAGMA busy_timeout = 5000"),
    };
    for (const QString &pragma : pragmas) {
        if (!exec(pragma, error)) {
            close();
            return false;
        }
    }
    return true;
}

void Database::close()
{
    if (!QSqlDatabase::contains(m_connectionName))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_path.clear();
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

int Database::schemaVersion() const
{
    QSqlQuery q(handle());
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next())
        return -1;
    return q.value(0).toInt();
}

int Database::latestSchemaVersion()
{
    const auto files = migrationFiles();
    return files.empty() ? 0 : files.rbegin()->first;
}

bool Database::migrate(QString *error)
{
    const int current = schemaVersion();
    if (current < 0) {
        setError(error, QStringLiteral("cannot read schema version"));
        return false;
    }

    const auto files = migrationFiles();
    const bool pending = !files.empty() && files.rbegin()->first > current;
    if (!pending)
        return true;

    // Back up an existing, non-empty database before touching its schema.
    if (current > 0 && m_path != QLatin1String(":memory:")) {
        const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString backup = QStringLiteral("%1.v%2-%3.bak").arg(m_path).arg(current).arg(stamp);
        if (!backupTo(backup, error))
            return false;
    }

    QSqlDatabase db = handle();
    for (const auto &[version, file] : files) {
        if (version <= current)
            continue;

        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            setError(error, QStringLiteral("cannot read %1").arg(file));
            return false;
        }
        const QString script = QString::fromUtf8(f.readAll());

        if (!db.transaction()) {
            setError(error, db.lastError().text());
            return false;
        }
        for (const QString &statement : splitStatements(script)) {
            if (!exec(statement, error)) {
                db.rollback();
                if (error)
                    *error = QStringLiteral("migration %1: %2").arg(version).arg(*error);
                return false;
            }
        }
        // PRAGMA takes no bound parameters; the version is our own integer.
        if (!exec(QStringLiteral("PRAGMA user_version = %1").arg(version), error) || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    return true;
}

bool Database::backupTo(const QString &destination, QString *error)
{
    if (QFile::exists(destination)) {
        setError(error, QStringLiteral("backup target %1 already exists").arg(destination));
        return false;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("VACUUM INTO ?"));
    q.addBindValue(destination);
    if (!q.exec()) {
        setError(error, q.lastError().text());
        return false;
    }
    return true;
}

QStringList Database::splitStatements(const QString &script)
{
    static const QRegularExpression triggerStart(
        QStringLiteral(R"(^\s*CREATE\s+(TEMP\s+|TEMPORARY\s+)?TRIGGER\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression endsWithEnd(QStringLiteral(R"(\bEND\s*$)"),
                                                QRegularExpression::CaseInsensitiveOption);

    QStringList statements;
    QString current;
    bool inSingle = false, inDouble = false, inLineComment = false, inBlockComment = false;

    const auto flush = [&] {
        const QString stmt = current.trimmed();
        current.clear();
        if (!stmt.isEmpty())
            statements.append(stmt);
    };

    for (qsizetype i = 0; i < script.size(); ++i) {
        const QChar c = script.at(i);
        const QChar next = i + 1 < script.size() ? script.at(i + 1) : QChar();

        if (inLineComment) {
            if (c == QLatin1Char('\n')) {
                inLineComment = false;
                current += c;
            }
            continue;
        }
        if (inBlockComment) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (!inSingle && !inDouble) {
            if (c == QLatin1Char('-') && next == QLatin1Char('-')) {
                inLineComment = true;
                continue;
            }
            if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
                inBlockComment = true;
                continue;
            }
        }

        if (c == QLatin1Char('\'') && !inDouble)
            inSingle = !inSingle;
        else if (c == QLatin1Char('"') && !inSingle)
            inDouble = !inDouble;

        if (c == QLatin1Char(';') && !inSingle && !inDouble) {
            // Inside a trigger body, semicolons separate the body's statements;
            // the trigger itself ends at END;
            if (triggerStart.match(current).hasMatch() && !endsWithEnd.match(current.trimmed()).hasMatch()) {
                current += c;
                continue;
            }
            flush();
            continue;
        }
        current += c;
    }
    flush();
    return statements;
}

bool Database::exec(const QString &sql, QString *error)
{
    QSqlQuery q(handle());
    if (!q.exec(sql)) {
        setError(error, QStringLiteral("%1 [%2]").arg(q.lastError().text(), sql.left(120)));
        return false;
    }
    return true;
}

} // namespace tumbler
