#pragma once

#include <QObject>

class QTimer;

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
    // How each platform notices (idlewatch_linux.cpp, idlewatch_mac.cpp):
    // they call wentAway() and cameBack().
    void startNoticing();
    void watchFor(int minutes); // 0: stop
    void wentAway(qint64 since);
    void cameBack(qint64 at);

    AppController *m_app;
    int m_minutes = -1;
    qint64 m_since = 0;
    int m_timeoutId = -1;      // Linux: KIdleTime's
    QTimer *m_poll = nullptr; // macOS: polling the idle time
};

} // namespace tumbler
