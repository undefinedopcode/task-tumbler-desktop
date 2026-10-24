#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace tumbler::ai {

// Checks a value against the subset of JSON Schema the app's AI schemas use:
// type (object, array, string, integer, number, boolean, null, or a list of
// these), properties, required, additionalProperties: false, items, enum.
// Returns an empty string when valid, otherwise the first problem with its
// path ("steps[2].minutes: expected integer"), which is also what a retry
// prompt tells the model.
QString validate(const QJsonValue &value, const QJsonObject &schema, const QString &path = QStringLiteral("$"));

} // namespace tumbler::ai
