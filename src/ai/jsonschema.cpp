#include "jsonschema.h"

#include <QJsonArray>
#include <cmath>

namespace tumbler::ai {

namespace {

bool hasType(const QJsonValue &v, const QString &type)
{
    if (type == QLatin1String("object"))
        return v.isObject();
    if (type == QLatin1String("array"))
        return v.isArray();
    if (type == QLatin1String("string"))
        return v.isString();
    if (type == QLatin1String("boolean"))
        return v.isBool();
    if (type == QLatin1String("null"))
        return v.isNull();
    if (type == QLatin1String("number"))
        return v.isDouble();
    if (type == QLatin1String("integer"))
        return v.isDouble() && std::floor(v.toDouble()) == v.toDouble();
    return false;
}

} // namespace

QString validate(const QJsonValue &value, const QJsonObject &schema, const QString &path)
{
    const QJsonValue type = schema.value(QLatin1String("type"));
    if (type.isString() && !hasType(value, type.toString()))
        return QStringLiteral("%1: expected %2").arg(path, type.toString());
    if (type.isArray()) {
        bool any = false;
        QStringList names;
        for (const QJsonValue &t : type.toArray()) {
            any |= hasType(value, t.toString());
            names.append(t.toString());
        }
        if (!any)
            return QStringLiteral("%1: expected %2").arg(path, names.join(QStringLiteral(" or ")));
    }

    if (schema.contains(QLatin1String("enum")) && !schema.value(QLatin1String("enum")).toArray().contains(value))
        return QStringLiteral("%1: not one of the allowed values").arg(path);

    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QJsonObject props = schema.value(QLatin1String("properties")).toObject();
        for (const QJsonValue &r : schema.value(QLatin1String("required")).toArray()) {
            if (!obj.contains(r.toString()))
                return QStringLiteral("%1.%2: missing").arg(path, r.toString());
        }
        const bool closed = schema.value(QLatin1String("additionalProperties")) == QJsonValue(false);
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (props.contains(it.key())) {
                const QString err = validate(it.value(), props.value(it.key()).toObject(), path + QLatin1Char('.') + it.key());
                if (!err.isEmpty())
                    return err;
            } else if (closed) {
                return QStringLiteral("%1.%2: not allowed").arg(path, it.key());
            }
        }
    }

    if (value.isArray() && schema.contains(QLatin1String("items"))) {
        const QJsonObject items = schema.value(QLatin1String("items")).toObject();
        const QJsonArray arr = value.toArray();
        for (qsizetype i = 0; i < arr.size(); ++i) {
            const QString err = validate(arr.at(i), items, QStringLiteral("%1[%2]").arg(path).arg(i));
            if (!err.isEmpty())
                return err;
        }
    }
    return {};
}

} // namespace tumbler::ai
