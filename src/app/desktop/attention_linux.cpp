#include "attention.h"

#include <QApplication>
#include <QWidget>

namespace tumbler::attention {

void request(QWidget *window, bool urgent)
{
    // The window manager marks it as wanting attention (KDE's taskbar
    // flashes the entry). Nothing to flash while it's closed to the tray;
    // the notification carries it then.
    if (window && window->isVisible())
        QApplication::alert(window, urgent ? 0 : 3000);
}

} // namespace tumbler::attention
