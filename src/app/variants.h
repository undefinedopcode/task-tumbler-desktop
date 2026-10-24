#pragma once

#include "entities.h"
#include "today.h"

#include <QVariantList>
#include <QVariantMap>

namespace tumbler {

// Core structs as plain maps for QML. Times are epoch milliseconds (QML turns
// them into Dates in the system zone), dates are ISO strings.

QString describeRecurrence(const RecurrencePattern &p);

QVariantMap toVariant(const Task &t);
QVariantMap toVariant(const Category &c);
QVariantMap toVariant(const AgendaItem &item);
QVariantMap toVariant(const ScheduleBlock &s);
QVariantMap toVariant(const WorkLog &w);

// A category's per-weekday windows as [{enabled, start, end}] x 7, Sunday first.
QVariantList scheduleToVariant(const CategorySchedule &s);
CategorySchedule scheduleFromVariant(const QVariantList &days);

QVariantList toVariantList(const QList<AgendaItem> &items);

} // namespace tumbler
