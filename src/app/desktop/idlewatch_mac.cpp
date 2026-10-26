#include "idlewatch.h"

#include "presence.h"

#include <QDateTime>
#include <QTimer>

namespace tumbler {

// macOS has no idle notifications to subscribe to, so look every few seconds.
namespace {
constexpr int kPollMs = 5000;
}

void IdleWatch::startNoticing()
{
    m_poll = new QTimer(this);
    m_poll->setInterval(kPollMs);
    connect(m_poll, &QTimer::timeout, this, [this] {
        const qint64 idle = presence::idleMs();
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (idle >= m_minutes * 60000LL)
            wentAway(now - idle);
        else if (idle < kPollMs * 2)
            cameBack(now - idle); // when the input came, not when we looked
    });
}

void IdleWatch::watchFor(int minutes)
{
    if (minutes > 0)
        m_poll->start();
    else
        m_poll->stop();
}

} // namespace tumbler
