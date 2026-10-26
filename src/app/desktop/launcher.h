#pragma once

#include <QObject>
#include <QVariantMap>

namespace tumbler {

class AppController;

// Opens what a task's launchers point at, when AppController asks. All the
// decisions (which actions, whether a command is approved) are made there;
// this only executes, the desktop's way (apps.h).
class Launcher : public QObject
{
    Q_OBJECT
public:
    explicit Launcher(AppController *app, QObject *parent = nullptr);

private:
    void open(const QVariantMap &action);
    AppController *m_app;
};

} // namespace tumbler
