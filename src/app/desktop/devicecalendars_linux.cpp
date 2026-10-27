#include "devicecalendars.h"

// Not yet on Linux: subscribe by address instead (Nextcloud, Google…).
namespace tumbler::devicecalendars {

bool supported()
{
    return false;
}

Access access()
{
    return Access::Denied;
}

void requestAccess(QObject *, std::function<void(bool)> done)
{
    done(false);
}

QList<Calendar> calendars()
{
    return {};
}

QList<Occurrence> occurrences(const QString &, const QDateTime &, const QDateTime &, QString *error)
{
    if (error)
        *error = QStringLiteral("calendars on this computer aren't supported here");
    return {};
}

void watch(QObject *, std::function<void()>)
{
}

} // namespace tumbler::devicecalendars
