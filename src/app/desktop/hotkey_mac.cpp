#include "hotkey.h"

#include <Carbon/Carbon.h>
#include <QAction>
#include <QPointer>

namespace tumbler {

// Carbon's hot keys are still the way to get a system-wide shortcut without
// asking for accessibility access.
namespace {
QPointer<QAction> g_action;

OSStatus onHotKey(EventHandlerCallRef, EventRef, void *)
{
    if (g_action)
        QMetaObject::invokeMethod(g_action, &QAction::trigger, Qt::QueuedConnection);
    return noErr;
}
} // namespace

void registerGlobalShortcut(QAction *action)
{
    g_action = action;
    static bool installed = false;
    if (!installed) {
        const EventTypeSpec pressed{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&onHotKey, 1, &pressed, nullptr, nullptr);
        installed = true;
    }
    const EventHotKeyID id{'TTcp', 1};
    EventHotKeyRef ref = nullptr;
    RegisterEventHotKey(kVK_Space, cmdKey | shiftKey, id, GetApplicationEventTarget(), 0, &ref);
}

} // namespace tumbler
