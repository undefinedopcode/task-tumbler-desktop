#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace tumbler {

// Owns one SQLite connection: opening, pragmas, schema migrations and backups.
// Migrations are numbered SQL files embedded under :/migrations
// (001_initial.sql, 002_...). PRAGMA user_version records the last one applied.
class Database
{
public:
    explicit Database(const QString &connectionName = QStringLiteral("tumbler"));
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // ~/.local/share/task-tumbler/tumbler.db on Linux.
    static QString defaultPath();

    // Opens (creating if needed) the file at `path`; ":memory:" works too.
    bool open(const QString &path, QString *error = nullptr);
    void close();

    QSqlDatabase handle() const;
    QString path() const { return m_path; }

    int schemaVersion() const;
    static int latestSchemaVersion();

    // Applies every embedded migration newer than schemaVersion(), each in
    // its own transaction. File databases are backed up first.
    bool migrate(QString *error = nullptr);

    // Consistent copy via VACUUM INTO; safe while the connection is open.
    bool backupTo(const QString &destination, QString *error = nullptr);

    // Splits a SQL script into statements, keeping CREATE TRIGGER ... END
    // bodies whole. Public for tests.
    static QStringList splitStatements(const QString &script);

private:
    bool exec(const QString &sql, QString *error);

    QString m_connectionName;
    QString m_path;
};

} // namespace tumbler
