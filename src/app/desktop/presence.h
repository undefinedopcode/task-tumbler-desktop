#pragma once

#include <QtGlobal>

namespace tumbler::presence {

// Whether the user is at the computer. presence_linux.cpp / presence_mac.cpp.

// Milliseconds since the last keyboard or mouse input.
qint64 idleMs();
// Away: no input for `minutes`, or the screen is locked.
bool away(int minutes);

} // namespace tumbler::presence
