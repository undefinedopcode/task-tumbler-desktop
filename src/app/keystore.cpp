#include "keystore.h"

#include <qt6keychain/keychain.h>

namespace tumbler {

namespace {
const QString kService = QStringLiteral("org.kde.tasktumbler");
}

void KeyStore::read(const QString &key, std::function<void(const QString &, const QString &)> done)
{
    auto *job = new QKeychain::ReadPasswordJob(kService, this);
    job->setKey(key);
    job->setAutoDelete(true);
    connect(job, &QKeychain::Job::finished, this, [job, done] {
        if (job->error() == QKeychain::EntryNotFound)
            done({}, {});
        else if (job->error() != QKeychain::NoError)
            done({}, job->errorString());
        else
            done(job->textData(), {});
    });
    job->start();
}

void KeyStore::write(const QString &key, const QString &value, std::function<void(const QString &)> done)
{
    QKeychain::Job *job;
    if (value.isEmpty()) {
        auto *del = new QKeychain::DeletePasswordJob(kService, this);
        del->setKey(key);
        job = del;
    } else {
        auto *w = new QKeychain::WritePasswordJob(kService, this);
        w->setKey(key);
        w->setTextData(value);
        job = w;
    }
    job->setAutoDelete(true);
    connect(job, &QKeychain::Job::finished, this, [job, done] {
        const bool ok = job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound;
        done(ok ? QString() : job->errorString());
    });
    job->start();
}

} // namespace tumbler
