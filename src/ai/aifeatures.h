#pragma once

#include "captureparser.h"
#include "dayplanner.h"
#include "entities.h"
#include "provider.h"

#include <QHash>
#include <QList>
#include <QSet>

namespace tumbler::ai {

// Runs a request and checks the answer against the request's schema. One
// retry, with the problem spelled out, before giving up.
void runValidated(Provider *provider, const Request &request, Callback done);

// ---- Plan my day --------------------------------------------------------------
// The model ranks and filters; dayplanner.cpp still computes every time, so
// even a small local model can't produce an impossible schedule.

struct PlanContext {
    QDate day;
    QString nowText;                        // "Sunday 25 October, 14:05"
    std::optional<int> energy;              // latest check-in, 1-10
    QString peakHours;                      // "09:00-12:00"
    QString userHints;                      // from Settings
    QList<Task> tasks;                      // candidates only
    QHash<QString, QString> categoryHours;  // category id -> "Work (Mon-Fri 09:00-17:00)"
    QList<Dependency> dependencies;
    QStringList fixedToday;                 // "10:00-11:00 Physio (event)"
    bool includeNotes = false;              // task descriptions may leave the machine
};

Request planRequest(const PlanContext &context);
// Unknown ids are dropped: the model never invents work.
PlanGuidance toGuidance(const QJsonObject &answer, const QSet<QString> &taskIds);

// ---- Break a task into steps --------------------------------------------------

struct Step {
    QString title;
    int minutes = 15;
    Energy energy = Energy::Medium;
};

Request breakdownRequest(const Task &task, const QList<Task> &existingSteps, bool includeNotes, const QString &userHints);
QList<Step> toSteps(const QJsonObject &answer);

// ---- Capture --------------------------------------------------------------------
// For lines the offline parser can't make sense of.

Request captureRequest(const QString &text, QDate today, const QStringList &categoryNames);
CaptureResult toCapture(const QJsonObject &answer, QDate today);

} // namespace tumbler::ai
