#pragma once

#include <QString>

namespace tumbler {

// Starts the app (to the tray) at login: ~/.config/autostart on Linux
// (autostart_linux.cpp), a LaunchAgent on macOS (autostart_mac.cpp).
bool setAutostart(bool enabled, QString *error = nullptr);
bool isAutostartEnabled();

} // namespace tumbler
