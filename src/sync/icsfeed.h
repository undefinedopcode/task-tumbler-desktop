#pragma once

#include "sync.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <functional>

namespace tumbler::sync {

// A subscribed calendar: an .ics URL (Nextcloud/CalDAV export links,
// Google's secret iCal address, webcal://) or a local file.
struct FeedSettings {
    QString id;
    QString name;
    QString url;
    QString color = QStringLiteral("#64748b");
    BlockingMode blocking = BlockingMode::BlockAll; // how its busy events treat task scheduling
    QStringList allowedTags;
    bool enabled = true;

    QJsonObject toJson() const;
    static FeedSettings fromJson(const QJsonObject &o);
};

// Occurrences in [from, to): repeats expanded, exceptions applied,
// cancelled events dropped, "free" (transparent) events kept but not blocking.
QList<RemoteEvent> parseIcs(const QByteArray &ics, const FeedSettings &feed, const QDateTime &from, const QDateTime &to,
                            QString *error = nullptr);

// Gets a feed's text: http(s), webcal (as https) or a local file.
class FeedFetcher : public QObject
{
    Q_OBJECT
public:
    explicit FeedFetcher(QNetworkAccessManager *network, QObject *parent = nullptr);
    static QUrl normalise(const QString &url);
    void fetch(const QString &url, std::function<void(const QByteArray &data, const QString &error)> done);

private:
    QNetworkAccessManager *m_network;
};

} // namespace tumbler::sync
