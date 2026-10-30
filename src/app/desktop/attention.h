#pragma once

class QWidget;

namespace tumbler::attention {

// Asks for the user's eye when the app isn't the one in front: the Dock icon
// bounces (macOS) or the taskbar entry flashes. Urgent keeps it up until the
// app is looked at; otherwise it's brief. attention_mac.mm / attention_linux.cpp.
void request(QWidget *window, bool urgent);

} // namespace tumbler::attention
