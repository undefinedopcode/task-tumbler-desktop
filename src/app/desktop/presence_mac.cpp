#include "presence.h"

#include <CoreGraphics/CoreGraphics.h>

namespace tumbler::presence {

qint64 idleMs()
{
    return qint64(CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType) * 1000);
}

namespace {
bool screenLocked()
{
    CFDictionaryRef session = CGSessionCopyCurrentDictionary();
    if (!session)
        return false;
    const auto locked = static_cast<CFBooleanRef>(CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked")));
    const bool result = locked && CFBooleanGetValue(locked);
    CFRelease(session);
    return result;
}
} // namespace

bool away(int minutes)
{
    return idleMs() >= minutes * 60000LL || screenLocked();
}

} // namespace tumbler::presence
