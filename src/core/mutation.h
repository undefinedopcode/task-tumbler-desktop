#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace tumbler {

// The rows a mutation may touch: SELECT * FROM <table> WHERE <where>.
// Scopes must cover every row the mutation inserts, updates or deletes,
// including rows reached by ON DELETE CASCADE / SET NULL, so that undo can
// put all of them back. Ids for new rows are generated before the mutation
// is built so its scopes can name them.
struct Scope {
    QString table;
    QString where;
    QVariantList binds;
};

struct Mutation {
    QString text; // shown in Undo/Redo menus
    QList<Scope> scopes;
    std::function<bool(QSqlDatabase db, QString *error)> apply;
};

// Several mutations applied in order as one undoable step.
Mutation combine(const QString &text, const QList<Mutation> &parts);

struct TableRows {
    QString table;
    QList<QVariantMap> rows;
};

// What a mutation changed: the scoped rows before and after it ran.
struct AppliedMutation {
    QString text;
    QList<TableRows> before;
    QList<TableRows> after;

    QSet<QString> tables() const;
    bool isNoop() const;
};

// Runs mutations in a transaction and can move the database between the
// before and after states of an applied one (undo / redo).
class MutationRunner
{
public:
    explicit MutationRunner(QSqlDatabase db);

    std::optional<AppliedMutation> run(const Mutation &mutation, QString *error = nullptr);
    bool revert(const AppliedMutation &applied, QString *error = nullptr);
    bool reapply(const AppliedMutation &applied, QString *error = nullptr);

private:
    // `alsoKeysFrom` re-reads those rows by primary key, so a row that drifted
    // out of a scope's WHERE (its parent_task_id was nulled, say) is still seen.
    std::optional<QList<TableRows>> snapshot(const QList<Scope> &scopes, QString *error,
                                             const QList<TableRows> *alsoKeysFrom = nullptr);
    bool transition(const QList<TableRows> &from, const QList<TableRows> &to, QString *error);
    QStringList primaryKey(const QString &table);

    QSqlDatabase m_db;
    QHash<QString, QStringList> m_primaryKeys;
};

} // namespace tumbler
