#pragma once

#include <QObject>

namespace tumbler {

class AppController;

// Notices time away from the keyboard during a work session. On return it
// asks (notification, and a banner on Today) whether to keep that time or
// leave it out. It never stops the timer on its own.
class IdleWatch : public QObject
{
    Q_OBJECT
public:
    explicit IdleWatch(AppController *app, QObject *parent = nullptr);

private:
    void configure();
    void notifyReturn();

    AppController *m_app;
    int m_timeoutId = -1;
    int m_minutes = -1;
    qint64 m_since = 0;
};

} // namespace tumbler
