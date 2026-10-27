#include "appscheme.h"

#include <QBuffer>
#include <QFile>
#include <QIcon>
#include <QPixmap>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrlQuery>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace tumbler {

void AppScheme::registerScheme()
{
    QWebEngineUrlScheme scheme(name);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    // Secure, so the page is a secure context (clipboard, crypto); local, so
    // it can't be reached from the web; fetch() and CORS, so the page can
    // ask it for things (icons) like any same-origin server.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed | QWebEngineUrlScheme::ContentSecurityPolicyIgnored
                    | QWebEngineUrlScheme::CorsEnabled | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

void AppScheme::requestStarted(QWebEngineUrlRequestJob *job)
{
    if (job->requestMethod() != "GET") {
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }
    QString path = job->requestUrl().path();
    // /icon/<theme name or absolute path>?size=N: a desktop icon as PNG (app
    // launchers name theme icons, which a web page can't draw).
    if (path.startsWith(QLatin1String("/icon/"))) {
        const QString name = QUrl::fromPercentEncoding(path.mid(6).toUtf8());
        const int size = qBound(16, QUrlQuery(job->requestUrl()).queryItemValue(QStringLiteral("size")).toInt(), 256);
        // A path: an app bundle or folder gets the system's icon for it (the
        // real app icon on macOS), anything else is read as an image.
        const QFileInfo file(name);
        const QIcon icon = !file.isAbsolute() ? QIcon::fromTheme(name)
            : file.isDir()                    ? QFileIconProvider().icon(file)
                                              : QIcon(name);
        if (icon.isNull()) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        auto *buffer = new QBuffer(job);
        buffer->open(QIODevice::WriteOnly);
        icon.pixmap(size, size).save(buffer, "PNG");
        buffer->close();
        buffer->open(QIODevice::ReadOnly);
        job->reply("image/png", buffer);
        return;
    }
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
