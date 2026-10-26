#include "idlewatch.h"

#include <KIdleTime>
#include <QDateTime>

namespace tumbler {

// KIdleTime tells us when the timeout passes and when input comes back.
void IdleWatch::startNoticing()
{
    KIdleTime *idle = KIdleTime::instance();
    connect(idle, &KIdleTime::timeoutReached, this, [this](int id, int msec) {
        if (id != m_timeoutId)
            return;
        wentAway(QDateTime::currentMSecsSinceEpoch() - msec);
        KIdleTime::instance()->catchNextResumeEvent();
    });
    connect(idle, &KIdleTime::resumingFromIdle, this, [this] { cameBack(QDateTime::currentMSecsSinceEpoch()); });
}

void IdleWatch::watchFor(int minutes)
{
    if (m_timeoutId >= 0)
        KIdleTime::instance()->removeIdleTimeout(m_timeoutId);
    m_timeoutId = minutes > 0 ? KIdleTime::instance()->addIdleTimeout(minutes * 60 * 1000) : -1;
}

} // namespace tumbler
