#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

class KStatusNotifierItem;
class QAction;
class QWindow;

namespace tumbler {

class AppController;

// System tray entry. Left click toggles the window; the tooltip says what is
// being worked on (with elapsed time) or what is next; the menu offers quick
// capture and the work timer.
class Tray : public QObject
{
    Q_OBJECT
public:
    Tray(AppController *app, QWindow *window, QObject *parent = nullptr);

private:
    void update();

    AppController *m_app;
    KStatusNotifierItem *m_item;
    QAction *m_timerAction;
    QTimer m_refresh;
};

} // namespace tumbler
