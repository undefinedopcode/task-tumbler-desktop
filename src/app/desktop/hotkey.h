#pragma once

class QAction;

namespace tumbler {

// Triggers `action` from anywhere, even with the app in the background.
// Linux: through KGlobalAccel, rebindable in System Settings (Meta+Shift+Space
// to start with). macOS: Cmd+Shift+Space, fixed for now.
void registerGlobalShortcut(QAction *action);

} // namespace tumbler
