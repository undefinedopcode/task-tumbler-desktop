#include "apps.h"

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>
#include <KIO/OpenUrlJob>
#include <KService>
#include <QUrl>

namespace tumbler::apps {

namespace {
App toApp(const KService::Ptr &s)
{
    return {s->desktopEntryName(), s->name(), s->icon()};
}

void reportErrors(KJob *job, QObject *context, Failed failed)
{
    QObject::connect(job, &KJob::result, context, [job, failed] {
        if (job->error())
            failed(job->errorString());
    });
    job->start();
}
} // namespace

QList<App> installed()
{
    QList<App> out;
    for (const KService::Ptr &s : KApplicationTrader::query([](const KService::Ptr &s) { return !s->noDisplay(); }))
        out.append(toApp(s));
    return out;
}

std::optional<App> find(const QString &id)
{
    if (const KService::Ptr s = KService::serviceByDesktopName(id))
        return toApp(s);
    return std::nullopt;
}

void openUrl(const QUrl &url, QObject *context, Failed failed)
{
    reportErrors(new KIO::OpenUrlJob(url), context, std::move(failed));
}

void launch(const QString &id, QObject *context, Failed failed)
{
    const KService::Ptr service = KService::serviceByDesktopName(id);
    if (!service) {
        failed(QStringLiteral("that app isn't installed"));
        return;
    }
    reportErrors(new KIO::ApplicationLauncherJob(service), context, std::move(failed));
}

} // namespace tumbler::apps
