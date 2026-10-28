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

// Repairs the shape slips models make, local ones especially, where the
// meaning is still clear: a map of id -> text where a list of {id, text}
// objects belongs, one item or a string where a list belongs, numbers as
// strings, an enum value in the wrong case, the answer wrapped in an outer
// key, extra keys, and an empty list or text left out. Anything it can't
// repair is left for validate() to report.
QJsonValue coerce(const QJsonValue &value, const QJsonObject &schema);

// A JSON skeleton of the schema with placeholder values, for prompts:
// models copy an example more reliably than they follow a schema.
QString example(const QJsonObject &schema);

} // namespace tumbler::ai
