#include "database.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace tumbler;

namespace {
QVariant scalar(const Database &db, const QString &sql)
{
    QSqlQuery q(db.handle());
    if (!q.exec(sql) || !q.next())
        return {};
    return q.value(0);
}

bool run(const Database &db, const QString &sql)
{
    QSqlQuery q(db.handle());
    return q.exec(sql);
}
} // namespace

class TestDatabase : public QObject
{
    Q_OBJECT

private slots:
    void migratesFreshDatabase()
    {
        Database db;
        QString error;
        QVERIFY2(db.open(QStringLiteral(":memory:"), &error), qPrintable(error));
        QCOMPARE(db.schemaVersion(), 0);
        QVERIFY2(db.migrate(&error), qPrintable(error));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QVERIFY(Database::latestSchemaVersion() >= 1);
        // Running again is a no-op.
        QVERIFY(db.migrate(&error));
        QCOMPARE(scalar(db, "PRAGMA foreign_keys").toInt(), 1);
    }

    void constraintsHold()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")) && db.migrate());

        QVERIFY(run(db, "INSERT INTO tasks (id, title, created_at, updated_at) VALUES ('t1', 'A', 0, 0)"));
        QVERIFY(!run(db, "INSERT INTO tasks (id, title, status, created_at, updated_at) VALUES ('t2', 'B', 'nope', 0, 0)"));
        QVERIFY(!run(db, "INSERT INTO tasks (id, title, tags, created_at, updated_at) VALUES ('t3', 'C', 'not json', 0, 0)"));
        QVERIFY(!run(db, "INSERT INTO task_dependencies VALUES ('t1', 't1', 0)"));
        QVERIFY(!run(db, "INSERT INTO task_dependencies VALUES ('t1', 'missing', 0)"));
        QVERIFY(!run(db, "INSERT INTO events (id, title, is_all_day, created_at, updated_at) VALUES ('e1', 'x', 0, 0, 0)"));

        // Deleting a task cascades to its occurrences.
        QVERIFY(run(db, "INSERT INTO task_occurrences (id, task_id, occurrence_date, created_at) VALUES ('o1', 't1', '2026-03-01', 0)"));
        QVERIFY(!run(db, "INSERT INTO task_occurrences (id, task_id, occurrence_date, created_at) VALUES ('o2', 't1', '2026-03-01', 0)"));
        QVERIFY(run(db, "DELETE FROM tasks WHERE id = 't1'"));
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM task_occurrences").toInt(), 0);
    }

    void notesFullTextSearch()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")) && db.migrate());
        QVERIFY(run(db, "INSERT INTO notes (id, title, content, created_at, updated_at) VALUES "
                        "('n1', 'Groceries', 'oat milk and lentils', 0, 0), ('n2', 'Ideas', 'tumbler desktop', 0, 0)"));
        const auto match = [&](const char *term) {
            return scalar(db, QStringLiteral("SELECT group_concat(n.id) FROM notes_fts f JOIN notes n ON n.rowid = f.rowid "
                                             "WHERE notes_fts MATCH '%1'").arg(QLatin1String(term)))
                .toString();
        };
        QCOMPARE(match("lentils"), QStringLiteral("n1"));
        QVERIFY(run(db, "UPDATE notes SET content = 'beans' WHERE id = 'n1'"));
        QCOMPARE(match("lentils"), QString());
        QCOMPARE(match("beans"), QStringLiteral("n1"));
        QVERIFY(run(db, "DELETE FROM notes WHERE id = 'n2'"));
        QCOMPARE(match("tumbler"), QString());
    }

    void fileDatabaseUsesWalAndBacksUp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("nested/tumbler.db");

        Database db;
        QString error;
        QVERIFY2(db.open(path, &error), qPrintable(error));
        QVERIFY(db.migrate(&error));
        QCOMPARE(scalar(db, "PRAGMA journal_mode").toString(), QStringLiteral("wal"));
        QVERIFY(run(db, "INSERT INTO settings VALUES ('timezone', '\"America/Vancouver\"')"));

        const QString backup = dir.filePath("copy.db");
        QVERIFY2(db.backupTo(backup, &error), qPrintable(error));
        QVERIFY(!db.backupTo(backup, &error)); // never overwrites

        Database copy(QStringLiteral("copy"));
        QVERIFY(copy.open(backup));
        QCOMPARE(scalar(copy, "SELECT value FROM settings WHERE key = 'timezone'").toString(),
                 QStringLiteral("\"America/Vancouver\""));
        QCOMPARE(copy.schemaVersion(), Database::latestSchemaVersion());
    }

    void splitsTriggersWhole()
    {
        const QStringList parts = Database::splitStatements(QStringLiteral(
            "-- comment; with semicolon\n"
            "CREATE TABLE a (x TEXT DEFAULT 'a;b');\n"
            "/* block; comment */\n"
            "CREATE TRIGGER t AFTER INSERT ON a BEGIN\n"
            "  INSERT INTO a VALUES ('1');\n"
            "  INSERT INTO a VALUES ('2');\n"
            "END;\n"
            "SELECT 1"));
        QCOMPARE(parts.size(), 3);
        QVERIFY(parts[0].contains("'a;b'"));
        QVERIFY(parts[1].startsWith("CREATE TRIGGER"));
        QVERIFY(parts[1].endsWith("END"));
        QCOMPARE(parts[2], QStringLiteral("SELECT 1"));
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
