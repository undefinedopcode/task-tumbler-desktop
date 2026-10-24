#include "mutation.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <map>

namespace tumbler {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString keyOf(const QVariantMap &row, const QStringList &pk)
{
    QStringList parts;
    for (const QString &col : pk)
        parts.append(row.value(col).toString());
    return parts.join(QChar(0x1f));
}

} // namespace

Mutation combine(const QString &text, const QList<Mutation> &parts)
{
    Mutation m;
    m.text = text;
    for (const Mutation &p : parts)
        m.scopes += p.scopes;
    m.apply = [parts](QSqlDatabase db, QString *error) {
        for (const Mutation &p : parts) {
            if (!p.apply(db, error))
                return false;
        }
        return true;
    };
    return m;
}

QSet<QString> AppliedMutation::tables() const
{
    QSet<QString> out;
    for (const TableRows &t : before)
        out.insert(t.table);
    return out;
}

bool AppliedMutation::isNoop() const
{
    if (before.size() != after.size())
        return false;
    for (qsizetype i = 0; i < before.size(); ++i) {
        if (before[i].rows != after[i].rows)
            return false;
    }
    return true;
}

MutationRunner::MutationRunner(QSqlDatabase db)
    : m_db(db)
{
}

QStringList MutationRunner::primaryKey(const QString &table)
{
    auto it = m_primaryKeys.constFind(table);
    if (it != m_primaryKeys.constEnd())
        return *it;

    // PRAGMA table_info lists pk position (1-based) for key columns.
    std::map<int, QString> ordered;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        while (q.next()) {
            const int pos = q.value(5).toInt();
            if (pos > 0)
                ordered.emplace(pos, q.value(1).toString());
        }
    }
    QStringList pk;
    for (const auto &[pos, name] : ordered)
        pk.append(name);
    m_primaryKeys.insert(table, pk);
    return pk;
}

std::optional<QList<TableRows>> MutationRunner::snapshot(const QList<Scope> &scopes, QString *error,
                                                         const QList<TableRows> *alsoKeysFrom)
{
    // One entry per table, rows merged across scopes and de-duplicated by key,
    // in first-seen table order so restores are deterministic.
    QList<TableRows> out;
    QHash<QString, qsizetype> index;
    QHash<QString, QSet<QString>> seen;

    for (const Scope &scope : scopes) {
        const QStringList pk = primaryKey(scope.table);
        if (pk.isEmpty()) {
            setError(error, QStringLiteral("table %1 has no primary key").arg(scope.table));
            return std::nullopt;
        }
        if (!index.contains(scope.table)) {
            index.insert(scope.table, out.size());
            out.append({scope.table, {}});
        }
        TableRows &target = out[index.value(scope.table)];

        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT * FROM %1 WHERE %2").arg(scope.table, scope.where));
        for (const QVariant &b : scope.binds)
            q.addBindValue(b);
        if (!q.exec()) {
            setError(error, QStringLiteral("snapshot %1: %2").arg(scope.table, q.lastError().text()));
            return std::nullopt;
        }
        while (q.next()) {
            const QSqlRecord rec = q.record();
            QVariantMap row;
            for (int i = 0; i < rec.count(); ++i)
                row.insert(rec.fieldName(i), q.value(i));
            const QString key = keyOf(row, pk);
            if (seen[scope.table].contains(key))
                continue;
            seen[scope.table].insert(key);
            target.rows.append(row);
        }
    }

    if (alsoKeysFrom) {
        for (const TableRows &earlier : *alsoKeysFrom) {
            const QStringList pk = primaryKey(earlier.table);
            TableRows &target = out[index.value(earlier.table)];
            QStringList clause;
            for (const QString &col : pk)
                clause.append(col + QStringLiteral(" = ?"));
            for (const QVariantMap &row : earlier.rows) {
                const QString key = keyOf(row, pk);
                if (seen[earlier.table].contains(key))
                    continue;
                QSqlQuery q(m_db);
                q.prepare(QStringLiteral("SELECT * FROM %1 WHERE %2").arg(earlier.table, clause.join(QStringLiteral(" AND "))));
                for (const QString &col : pk)
                    q.addBindValue(row.value(col));
                if (!q.exec()) {
                    setError(error, QStringLiteral("snapshot %1: %2").arg(earlier.table, q.lastError().text()));
                    return std::nullopt;
                }
                if (!q.next())
                    continue; // deleted by the mutation
                const QSqlRecord rec = q.record();
                QVariantMap current;
                for (int i = 0; i < rec.count(); ++i)
                    current.insert(rec.fieldName(i), q.value(i));
                seen[earlier.table].insert(key);
                target.rows.append(current);
            }
        }
    }
    return out;
}

std::optional<AppliedMutation> MutationRunner::run(const Mutation &mutation, QString *error)
{
    if (!m_db.transaction()) {
        setError(error, m_db.lastError().text());
        return std::nullopt;
    }
    QSqlQuery(m_db).exec(QStringLiteral("PRAGMA defer_foreign_keys = ON"));

    AppliedMutation applied;
    applied.text = mutation.text;

    auto before = snapshot(mutation.scopes, error);
    if (!before || !mutation.apply(m_db, error)) {
        m_db.rollback();
        return std::nullopt;
    }
    auto after = snapshot(mutation.scopes, error, &*before);
    if (!after || !m_db.commit()) {
        if (after)
            setError(error, m_db.lastError().text());
        m_db.rollback();
        return std::nullopt;
    }
    applied.before = *before;
    applied.after = *after;
    return applied;
}

bool MutationRunner::revert(const AppliedMutation &applied, QString *error)
{
    return transition(applied.after, applied.before, error);
}

bool MutationRunner::reapply(const AppliedMutation &applied, QString *error)
{
    return transition(applied.before, applied.after, error);
}

bool MutationRunner::transition(const QList<TableRows> &from, const QList<TableRows> &to, QString *error)
{
    if (!m_db.transaction()) {
        setError(error, m_db.lastError().text());
        return false;
    }
    QSqlQuery(m_db).exec(QStringLiteral("PRAGMA defer_foreign_keys = ON"));

    const auto fail = [&](const QString &message) {
        setError(error, message);
        m_db.rollback();
        return false;
    };

    // Rows present on both sides are updated in place, never deleted and
    // re-inserted: a delete would fire ON DELETE CASCADE on rows outside the
    // scope. Only rows that exist on one side are deleted or inserted.
    for (qsizetype t = 0; t < to.size(); ++t) {
        const QString &table = to[t].table;
        const QStringList pk = primaryKey(table);

        QHash<QString, QVariantMap> fromRows, toRows;
        for (const QVariantMap &r : from.value(t).rows)
            fromRows.insert(keyOf(r, pk), r);
        for (const QVariantMap &r : to[t].rows)
            toRows.insert(keyOf(r, pk), r);

        const auto whereKey = [&pk](const QVariantMap &row, QSqlQuery &q) {
            for (const QString &col : pk)
                q.addBindValue(row.value(col));
        };
        QStringList pkClause;
        for (const QString &col : pk)
            pkClause.append(col + QStringLiteral(" = ?"));

        // Walk the snapshot lists, not the hashes, so the order is the same
        // on every run.
        for (const QVariantMap &row : from.value(t).rows) {
            if (toRows.contains(keyOf(row, pk)))
                continue;
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("DELETE FROM %1 WHERE %2").arg(table, pkClause.join(QStringLiteral(" AND "))));
            whereKey(row, q);
            if (!q.exec())
                return fail(QStringLiteral("restore %1: %2").arg(table, q.lastError().text()));
        }

        // Updates and inserts can trip a unique constraint purely because of
        // their order (moving the "default" flag from one row to another), so
        // a failing statement is retried after the others have run. Only a
        // pass that makes no progress is an error.
        QList<QVariantMap> pending;
        for (const QVariantMap &row : to[t].rows) {
            const auto existing = fromRows.constFind(keyOf(row, pk));
            if (existing == fromRows.cend() || *existing != row)
                pending.append(row);
        }
        QString lastError;
        while (!pending.isEmpty()) {
            QList<QVariantMap> failed;
            for (const QVariantMap &row : std::as_const(pending)) {
                QSqlQuery q(m_db);
                if (fromRows.contains(keyOf(row, pk))) {
                    QStringList sets;
                    for (auto c = row.cbegin(); c != row.cend(); ++c)
                        sets.append(c.key() + QStringLiteral(" = ?"));
                    q.prepare(QStringLiteral("UPDATE %1 SET %2 WHERE %3")
                                  .arg(table, sets.join(QLatin1Char(',')), pkClause.join(QStringLiteral(" AND "))));
                    for (auto c = row.cbegin(); c != row.cend(); ++c)
                        q.addBindValue(c.value());
                    whereKey(row, q);
                } else {
                    QStringList cols, marks;
                    for (auto c = row.cbegin(); c != row.cend(); ++c) {
                        cols.append(c.key());
                        marks.append(QStringLiteral("?"));
                    }
                    q.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                                  .arg(table, cols.join(QLatin1Char(',')), marks.join(QLatin1Char(','))));
                    for (auto c = row.cbegin(); c != row.cend(); ++c)
                        q.addBindValue(c.value());
                }
                if (!q.exec()) {
                    lastError = q.lastError().text();
                    failed.append(row);
                }
            }
            if (failed.size() == pending.size())
                return fail(QStringLiteral("restore %1: %2").arg(table, lastError));
            pending = failed;
        }
    }

    if (!m_db.commit())
        return fail(m_db.lastError().text());
    return true;
}

} // namespace tumbler
