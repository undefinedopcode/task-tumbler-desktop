#pragma once

#include <QObject>

namespace tumbler {

class AppController;

// Turns on the desktop's Do Not Disturb while focus mode is on, through the
// standard org.freedesktop.Notifications Inhibit call (Plasma, GNOME).
class FocusGuard : public QObject
{
    Q_OBJECT
public:
    explicit FocusGuard(AppController *app, QObject *parent = nullptr);
    ~FocusGuard() override;

private:
    void update();
    AppController *m_app;
    uint m_cookie = 0;
};

} // namespace tumbler
