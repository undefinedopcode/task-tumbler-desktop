#include "database.h"
#include "importer.h"
#include "recurrence.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QTest>

using namespace tumbler;

namespace {

QJsonObject loadFixture()
{
    QFile f(QStringLiteral(TUMBLER_FIXTURES "/export_quirks.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture");
    return QJsonDocument::fromJson(f.readAll()).object();
}

QVariant scalar(const Database &db, const QString &sql)
{
    QSqlQuery q(db.handle());
    if (!q.exec(sql) || !q.next())
        return {};
    return q.value(0);
}

qint64 utcMs(const char *iso)
{
    return QDateTime::fromString(QLatin1String(iso), Qt::ISODateWithMs).toMSecsSinceEpoch();
}

bool hasWarning(const ImportReport &r, const QString &fragment)
{
    for (const QString &w : r.warnings) {
        if (w.contains(fragment))
            return true;
    }
    return false;
}

} // namespace

class TestImporter : public QObject
{
    Q_OBJECT

private:
    Database m_db;
    ImportReport m_report;

private slots:
    void initTestCase()
    {
        QString error;
        QVERIFY(m_db.open(QStringLiteral(":memory:"), &error) && m_db.migrate(&error));
        Importer importer(m_db);
        QVERIFY2(importer.run(loadFixture(), &m_report, &error), qPrintable(error));
        QCOMPARE(m_report.timezone, QStringLiteral("America/Vancouver"));
    }

    void counts()
    {
        const QMap<QString, int> expected{
            {"settings", 4},       {"categories", 1},   {"tasks", 6},        {"task_dependencies", 1},
            {"task_occurrences", 4}, {"schedules", 2},  {"events", 5},       {"energy_logs", 1},
            {"health_logs", 1},    {"task_history", 1}, {"ai_logs", 1},      {"notes", 1},
            {"integrations", 1},   {"synced_items", 1}, {"synced_events", 1},
        };
        QCOMPARE(m_report.imported, expected);
        const QMap<QString, int> skipped{
            {"task_dependencies", 2}, {"task_occurrences", 1}, {"schedules", 1}, {"energy_logs", 1}, {"synced_items", 1},
        };
        QCOMPARE(m_report.skipped, skipped);
        for (auto it = expected.cbegin(); it != expected.cend(); ++it)
            QCOMPARE(scalar(m_db, QStringLiteral("SELECT COUNT(*) FROM %1").arg(it.key())).toInt(), it.value());
    }

    void defaultCategoryFromWorkHours()
    {
        QSqlQuery q(m_db.handle());
        QVERIFY(q.exec("SELECT id, name, schedule FROM categories WHERE is_default = 1") && q.next());
        QCOMPARE(q.value(1).toString(), QStringLiteral("Default"));
        const QJsonObject schedule = QJsonDocument::fromJson(q.value(2).toByteArray()).object();
        QCOMPARE(schedule["1"].toObject()["start"].toString(), QStringLiteral("13:00"));
        QCOMPARE(schedule["0"].toObject()["enabled"].toBool(), false);
        QCOMPARE(scalar(m_db, "SELECT COUNT(*) FROM tasks WHERE category_id = '" + q.value(0).toString() + "'").toInt(), 6);
    }

    void taskFieldsNormalised()
    {
        QCOMPARE(scalar(m_db, "SELECT tags FROM tasks WHERE id = 't-parent'").toString(), QStringLiteral("[\"travel\"]"));
        QCOMPARE(scalar(m_db, "SELECT tags FROM tasks WHERE id = 't-child'").toString(), QStringLiteral("[]"));
        QCOMPARE(scalar(m_db, "SELECT complexity FROM tasks WHERE id = 't-child'").toInt(), 5);
        // A UTC instant of local midnight is the local day.
        QCOMPARE(scalar(m_db, "SELECT due_date FROM tasks WHERE id = 't-child'").toString(), QStringLiteral("2025-11-20"));
        QCOMPARE(scalar(m_db, "SELECT parent_task_id FROM tasks WHERE id = 't-child'").toString(), QStringLiteral("t-parent"));
        QVERIFY(scalar(m_db, "SELECT parent_task_id FROM tasks WHERE id = 't-orphan'").isNull());
        QCOMPARE(scalar(m_db, "SELECT status FROM tasks WHERE id = 't-orphan'").toString(), QStringLiteral("pending"));
        QCOMPARE(scalar(m_db, "SELECT is_fixed_time FROM tasks WHERE id = 't-daily'").toInt(), 1);
        QCOMPARE(scalar(m_db, "SELECT created_at FROM tasks WHERE id = 't-parent'").toLongLong(), utcMs("2025-11-19T02:43:32Z"));
        QVERIFY(hasWarning(m_report, "t-orphan: parent gone"));
        QVERIFY(hasWarning(m_report, "unknown status 'weird'"));
    }

    void recurrenceGetsAnchored()
    {
        const auto pattern = [this](const char *id) {
            return RecurrencePattern::fromJson(
                QJsonDocument::fromJson(scalar(m_db, QStringLiteral("SELECT recurrence FROM tasks WHERE id = '%1'").arg(QLatin1String(id)))
                                            .toByteArray())
                    .object());
        };

        const auto daily = pattern("t-daily");
        QVERIFY(daily);
        QCOMPARE(daily->startDate, QDate(2025, 11, 19));

        // The web app put this fortnightly task on 12-08 and 12-22. Anchored
        // on its creation week it would land on 12-15 and 12-29 instead, so it
        // is re-anchored to keep the existing rhythm.
        const auto biweekly = pattern("t-biweekly");
        QVERIFY(biweekly);
        QCOMPARE(biweekly->startDate, QDate(2025, 12, 22));
        QVERIFY(biweekly->occursOn(QDate(2026, 1, 5)));
        QVERIFY(!biweekly->occursOn(QDate(2025, 12, 29)));
        QVERIFY(hasWarning(m_report, "re-anchored on 2025-12-22"));

        QVERIFY(scalar(m_db, "SELECT recurrence FROM tasks WHERE id = 't-broken'").isNull());
        QVERIFY(hasWarning(m_report, "t-broken: unusable recurrence"));
    }

    void occurrencesDeduplicated()
    {
        QSqlQuery q(m_db.handle());
        QVERIFY(q.exec("SELECT occurrence_date, status, completed_at, scheduled_time FROM task_occurrences "
                       "WHERE task_id = 't-daily' ORDER BY occurrence_date"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("2025-11-19"));
        QCOMPARE(q.value(1).toString(), QStringLiteral("completed")); // the row with an outcome wins
        QCOMPARE(q.value(2).toLongLong(), utcMs("2025-11-20T03:31:00Z"));
        QVERIFY(q.next());
        QCOMPARE(q.value(3).toString(), QStringLiteral("20:15"));
    }

    void dependencyCyclesDropped()
    {
        QCOMPARE(scalar(m_db, "SELECT task_id || '>' || depends_on_task_id FROM task_dependencies").toString(),
                 QStringLiteral("t-orphan>t-daily"));
        QVERIFY(hasWarning(m_report, "would form a cycle"));
    }

    void scheduleTimes()
    {
        // Naive schedule times were local: 12:15 in Vancouver in December is 20:15 UTC.
        QCOMPARE(scalar(m_db, "SELECT scheduled_start FROM schedules WHERE id = 's1'").toLongLong(), utcMs("2025-12-03T20:15:00Z"));
        QCOMPARE(scalar(m_db, "SELECT scheduled_start FROM schedules WHERE id = 's2'").toLongLong(), utcMs("2025-11-27T06:00:00Z"));
        QCOMPARE(scalar(m_db, "SELECT bumped_count FROM schedules WHERE id = 's2'").toInt(), 2);
        QVERIFY(scalar(m_db, "SELECT work_log_id FROM schedules WHERE id = 's2'").isNull());
        QVERIFY(hasWarning(m_report, "schedules: 1 rows had no timezone"));
    }

    void events()
    {
        // Timed events are wall-clock times in their own zone: 12:10 in
        // Vancouver in November is 20:10 UTC.
        QCOMPARE(scalar(m_db, "SELECT start_at FROM events WHERE id = 'e1'").toLongLong(), utcMs("2025-11-30T20:10:00Z"));
        // No zone recorded: the web app's Sydney default (UTC+11 in summer).
        QCOMPARE(scalar(m_db, "SELECT start_at FROM events WHERE id = 'e4'").toLongLong(), utcMs("2025-11-25T22:00:00Z"));
        QCOMPARE(scalar(m_db, "SELECT timezone FROM events WHERE id = 'e5'").toString(), QStringLiteral("Australia/Melbourne"));
        QVERIFY(hasWarning(m_report, "events: 1 entered in Australia/Sydney, not America/Vancouver; check their times: Hackathon"));
        QVERIFY(hasWarning(m_report, "events: 1 entered in Australia/Melbourne, not America/Vancouver; check their times: Ferry"));
        QVERIFY(!hasWarning(m_report, "Half-entered"));
        QVERIFY(!hasWarning(m_report, "Dentist"));
        QCOMPARE(scalar(m_db, "SELECT allowed_tags FROM events WHERE id = 'e1'").toString(), QStringLiteral("[\"phone\"]"));
        QCOMPARE(scalar(m_db, "SELECT end_date FROM events WHERE id = 'e2'").toString(), QStringLiteral("2025-12-26"));
        QCOMPARE(scalar(m_db, "SELECT blocking_mode FROM events WHERE id = 'e2'").toString(), QStringLiteral("block_all"));
        QCOMPARE(scalar(m_db, "SELECT is_all_day FROM events WHERE id = 'e3'").toInt(), 1);
    }

    void looseColumns()
    {
        QCOMPARE(scalar(m_db, "SELECT hours_slept FROM health_logs").toDouble(), 7.5);
        QCOMPARE(scalar(m_db, "SELECT source FROM health_logs").toString(), QStringLiteral("manual"));
        QVERIFY(scalar(m_db, "SELECT task_id FROM task_history").isNull());
        QCOMPARE(scalar(m_db, "SELECT request_context FROM ai_logs").toString(), QStringLiteral("{\"date\":\"2025-11-20\"}"));
        QCOMPARE(scalar(m_db, "SELECT created_at FROM notes").toLongLong(), utcMs("2026-03-01T10:00:00Z"));
        QCOMPARE(scalar(m_db, "SELECT value FROM settings WHERE key = 'timezone'").toString(), QStringLiteral("\"America/Vancouver\""));
        QVERIFY(scalar(m_db, "SELECT COUNT(*) FROM settings WHERE key = 'ai_scheduling_hints'").toInt() == 0);
    }

    void integrationsNeedReconnecting()
    {
        QCOMPARE(scalar(m_db, "SELECT sync_status FROM integrations").toString(), QStringLiteral("needs_auth"));
        QCOMPARE(scalar(m_db, "SELECT json_extract(config, '$.sync_limit') FROM integrations").toInt(), 2);
    }

    void refusesSecondImport()
    {
        Importer importer(m_db);
        QString error;
        QVERIFY(!importer.run(loadFixture(), nullptr, &error));
        QVERIFY(error.contains("already has data"));
    }

    void rejectsOtherFormats()
    {
        Database db(QStringLiteral("other"));
        QVERIFY(db.open(QStringLiteral(":memory:")) && db.migrate());
        Importer importer(db);
        QString error;
        QVERIFY(!importer.run(QJsonObject{{"format", "something-else"}, {"version", 1}}, nullptr, &error));
        QVERIFY(!importer.run(QJsonObject{{"format", "task-tumbler-export"}, {"version", 2}}, nullptr, &error));
        QVERIFY(error.contains("version 2"));
    }

    void failureWritesNothing()
    {
        // Two health logs for the same day violate a UNIQUE constraint midway
        // through; nothing imported before it may remain.
        QJsonObject doc = loadFixture();
        QJsonObject tables = doc["tables"].toObject();
        QJsonArray health = tables["health_logs"].toArray();
        QJsonObject dup = health.first().toObject();
        dup["id"] = "h2";
        health.append(dup);
        tables["health_logs"] = health;
        doc["tables"] = tables;

        Database db(QStringLiteral("rollback"));
        QVERIFY(db.open(QStringLiteral(":memory:")) && db.migrate());
        Importer importer(db);
        QString error;
        QVERIFY(!importer.run(doc, nullptr, &error));
        QVERIFY2(error.contains("health_logs"), qPrintable(error));
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM tasks").toInt(), 0);
        QCOMPARE(scalar(db, "SELECT COUNT(*) FROM settings").toInt(), 0);
    }
};

QTEST_GUILESS_MAIN(TestImporter)
#include "tst_importer.moc"
