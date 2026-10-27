#include "appscheme.h"

#include <QBuffer>
#include <QFile>
#include <QMimeDatabase>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace tumbler {

void AppScheme::registerScheme()
{
    QWebEngineUrlScheme scheme(name);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    // Secure, so the page is a secure context (clipboard, crypto); local, so
    // it can't be reached from the web.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
    QWebEngineUrlScheme::registerScheme(scheme);
}

void AppScheme::requestStarted(QWebEngineUrlRequestJob *job)
{
    if (job->requestMethod() != "GET") {
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }
    QString path = job->requestUrl().path();
    if (path.isEmpty() || path == QLatin1String("/") || !QFile::exists(QStringLiteral(":/webui") + path))
        path = QStringLiteral("/index.html");
    QFile file(QStringLiteral(":/webui") + path);
    if (!file.open(QIODevice::ReadOnly)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    auto *buffer = new QBuffer(job);
    buffer->setData(file.readAll());
    buffer->open(QIODevice::ReadOnly);
    job->reply(QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name().toUtf8(), buffer);
}

} // namespace tumbler
