#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>

namespace tumbler {

class AppController;
struct AgendaItem;

// Desktop notifications for today's agenda, with buttons that act directly:
// Start / Bump 15m / Done for blocks, Done / Skip for recurring items, Next
// free slot / Done for a missed block. When each fires is decided by
// dueReminders() in core.
class Reminders : public QObject
{
    Q_OBJECT
public:
    explicit Reminders(AppController *app, QObject *parent = nullptr);

private:
    void check();
    void notify(const QString &event, const QString &title, const QString &text, const AgendaItem &item, bool missed = false);

    AppController *m_app;
    QTimer m_timer;
    QSet<QString> m_fired;
    qint64 m_startedAt;
};

} // namespace tumbler
