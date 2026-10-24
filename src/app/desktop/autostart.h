#pragma once

#include <QString>

namespace tumbler {

// Starts the app (to the tray) at login via ~/.config/autostart.
bool setAutostart(bool enabled, QString *error = nullptr);
bool isAutostartEnabled();

} // namespace tumbler
