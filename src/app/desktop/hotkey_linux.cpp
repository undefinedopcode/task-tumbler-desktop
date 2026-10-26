#include "hotkey.h"

#include <KGlobalAccel>
#include <QAction>

namespace tumbler {

void registerGlobalShortcut(QAction *action)
{
    KGlobalAccel::self()->setGlobalShortcut(action, QKeySequence(Qt::META | Qt::SHIFT | Qt::Key_Space));
}

} // namespace tumbler
