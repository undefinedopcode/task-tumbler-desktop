// tumbler-import: load a web-app export into a new Task Tumbler database.
//
//   tumbler-import export.json [--db PATH] [--dry-run]
//
// Without --db the default database path is used. --dry-run imports into an
// in-memory database and prints the report without writing anything.

#include "database.h"
#include "importer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTextStream>

using namespace tumbler;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tumbler-import"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Import a Task Tumbler web export into the desktop database."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("export"), QStringLiteral("JSON file from export-desktop.ts"));
    const QCommandLineOption dbOption(QStringLiteral("db"), QStringLiteral("Database file (default: %1)").arg(Database::defaultPath()),
                                      QStringLiteral("path"), Database::defaultPath());
    const QCommandLineOption dryRun(QStringLiteral("dry-run"), QStringLiteral("Import into memory and report only."));
    parser.addOption(dbOption);
    parser.addOption(dryRun);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = parser.positionalArguments();
    if (args.size() != 1) {
        parser.showHelp(2);
    }

    QFile file(args.first());
    if (!file.open(QIODevice::ReadOnly)) {
        err << "cannot read " << args.first() << ": " << file.errorString() << Qt::endl;
        return 1;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        err << "not valid JSON: " << parseError.errorString() << Qt::endl;
        return 1;
    }

    const QString path = parser.isSet(dryRun) ? QStringLiteral(":memory:") : parser.value(dbOption);
    if (path != QLatin1String(":memory:") && QFileInfo::exists(path)) {
        err << path << " already exists; importing only into a new database." << Qt::endl;
        return 1;
    }

    Database db;
    QString error;
    if (!db.open(path, &error) || !db.migrate(&error)) {
        err << "database: " << error << Qt::endl;
        return 1;
    }

    Importer importer(db);
    ImportReport report;
    if (!importer.run(doc.object(), &report, &error)) {
        err << "import failed, nothing written: " << error << Qt::endl;
        db.close();
        if (path != QLatin1String(":memory:")) {
            for (const char *suffix : {"", "-wal", "-shm"})
                QFile::remove(path + QLatin1String(suffix));
        }
        return 1;
    }

    out << "Imported " << report.userEmail << " (" << report.sourceKind << ", local time " << report.timezone << ")"
        << Qt::endl;
    out << "  " << report.summary() << Qt::endl;
    for (const QString &w : report.warnings)
        out << "  warning: " << w << Qt::endl;
    out << (parser.isSet(dryRun) ? QStringLiteral("Dry run: nothing written.") : QStringLiteral("Wrote %1").arg(path))
        << Qt::endl;
    return 0;
}
