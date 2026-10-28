#include "jsonschema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
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

namespace {

// The schema's type, when it names just one (all the app's schemas do).
QString typeOf(const QJsonObject &schema)
{
    return schema.value(QLatin1String("type")).toString();
}

QStringList requiredOf(const QJsonObject &schema)
{
    QStringList out;
    for (const QJsonValue &r : schema.value(QLatin1String("required")).toArray())
        out.append(r.toString());
    return out;
}

QJsonValue coerceObject(const QJsonObject &obj, const QJsonObject &schema)
{
    const QJsonObject props = schema.value(QLatin1String("properties")).toObject();
    const QStringList required = requiredOf(schema);

    // {"plan": {...the answer...}}: one key wrapping an object, none of the
    // expected keys present.
    if (obj.size() == 1 && obj.begin().value().isObject() && !required.isEmpty()) {
        bool anyExpected = false;
        for (const QString &k : required)
            anyExpected |= obj.contains(k);
        if (!anyExpected && !props.contains(obj.begin().key()))
            return coerceObject(obj.begin().value().toObject(), schema);
    }

    QJsonObject out;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (props.contains(it.key()))
            out.insert(it.key(), coerce(it.value(), props.value(it.key()).toObject()));
        else if (schema.value(QLatin1String("additionalProperties")) != QJsonValue(false))
            out.insert(it.key(), it.value());
        // else: an extra key in a closed object, dropped
    }

    // An empty list or text left out, when the rest of the answer is there.
    if (!out.isEmpty()) {
        for (const QString &k : required) {
            if (out.contains(k))
                continue;
            const QString t = typeOf(props.value(k).toObject());
            if (t == QLatin1String("array"))
                out.insert(k, QJsonArray());
            else if (t == QLatin1String("string") && !props.value(k).toObject().contains(QLatin1String("enum")))
                out.insert(k, QString());
        }
    }
    return out;
}

QJsonValue coerceArray(const QJsonValue &value, const QJsonObject &schema)
{
    const QJsonObject items = schema.value(QLatin1String("items")).toObject();
    const QString itemType = typeOf(items);
    QJsonArray in;
    if (value.isArray()) {
        in = value.toArray();
    } else if (value.isNull() || value.isUndefined()) {
        return QJsonArray();
    } else if (value.isString()) {
        if (value.toString().trimmed().isEmpty())
            return QJsonArray();
        in.append(value);
    } else if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QStringList keys = requiredOf(items);
        // One item, not in a list, if it has any of an item's keys.
        bool isItem = false;
        for (const QString &k : keys)
            isItem |= itemType == QLatin1String("object") && obj.contains(k);
        if (isItem) {
            in.append(obj);
        } else if (itemType == QLatin1String("object") && keys.size() == 2) {
            // {"t1": "a note", "t2": "another"} for [{task_id, note}, ...],
            // or {"t1": {"reason": ...}} for [{task_id, reason}, ...].
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                QJsonObject item;
                if (it.value().isObject()) {
                    item = it.value().toObject();
                    for (const QString &k : keys) {
                        if (!item.contains(k)) {
                            item.insert(k, it.key());
                            break;
                        }
                    }
                } else {
                    item.insert(keys.at(0), it.key());
                    item.insert(keys.at(1), it.value());
                }
                in.append(item);
            }
        } else if (itemType == QLatin1String("string")) {
            for (auto it = obj.begin(); it != obj.end(); ++it)
                in.append(it.value().isString() ? it.value() : QJsonValue(it.key()));
        } else {
            return value; // no idea; validate() will say so
        }
    } else {
        in.append(value);
    }

    QJsonArray out;
    for (const QJsonValue &item : in) {
        QJsonValue v = item;
        // ["t1", ...] given as [{"task_id": "t1"}, ...]: the one string in it.
        if (itemType == QLatin1String("string") && v.isObject()) {
            const QJsonObject o = v.toObject();
            QStringList strings;
            for (auto it = o.begin(); it != o.end(); ++it) {
                if (it.value().isString())
                    strings.append(it.value().toString());
            }
            if (o.size() >= 1 && strings.size() >= 1 && (o.contains(QLatin1String("task_id")) || o.contains(QLatin1String("id"))))
                v = o.contains(QLatin1String("task_id")) ? o.value(QLatin1String("task_id")) : o.value(QLatin1String("id"));
            else if (strings.size() == 1)
                v = strings.first();
        }
        out.append(items.isEmpty() ? v : coerce(v, items));
    }
    return out;
}

} // namespace

QJsonValue coerce(const QJsonValue &value, const QJsonObject &schema)
{
    const QString type = typeOf(schema);
    if (type == QLatin1String("object"))
        return value.isObject() ? coerceObject(value.toObject(), schema) : value;
    if (type == QLatin1String("array"))
        return coerceArray(value, schema);
    if (type == QLatin1String("string")) {
        QJsonValue v = value;
        if (value.isDouble())
            v = QString::number(value.toDouble());
        else if (value.isBool())
            v = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        else if (value.isNull())
            v = QString();
        if (v.isString() && schema.contains(QLatin1String("enum"))) {
            const QString s = v.toString().trimmed();
            for (const QJsonValue &allowed : schema.value(QLatin1String("enum")).toArray()) {
                if (allowed.toString().compare(s, Qt::CaseInsensitive) == 0)
                    return allowed;
            }
        }
        return v;
    }
    if (type == QLatin1String("integer") || type == QLatin1String("number")) {
        double d = 0;
        bool ok = false;
        if (value.isDouble()) {
            d = value.toDouble();
            ok = true;
        } else if (value.isString()) {
            // "30", "30 min", "~45"
            static const QRegularExpression number(QStringLiteral(R"(-?\d+(\.\d+)?)"));
            const auto m = number.match(value.toString());
            if (m.hasMatch())
                d = m.captured(0).toDouble(&ok);
        }
        if (!ok)
            return value;
        return type == QLatin1String("integer") ? QJsonValue(qint64(std::llround(d))) : QJsonValue(d);
    }
    if (type == QLatin1String("boolean") && value.isString()) {
        const QString s = value.toString().trimmed().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("yes"))
            return true;
        if (s == QLatin1String("false") || s == QLatin1String("no"))
            return false;
    }
    return value;
}

namespace {

QJsonValue skeleton(const QJsonObject &schema)
{
    const QString type = typeOf(schema);
    if (type == QLatin1String("object")) {
        QJsonObject o;
        const QJsonObject props = schema.value(QLatin1String("properties")).toObject();
        for (auto it = props.begin(); it != props.end(); ++it)
            o.insert(it.key(), skeleton(it.value().toObject()));
        return o;
    }
    if (type == QLatin1String("array"))
        return QJsonArray{skeleton(schema.value(QLatin1String("items")).toObject())};
    if (type == QLatin1String("string")) {
        if (schema.contains(QLatin1String("enum"))) {
            QStringList values;
            for (const QJsonValue &v : schema.value(QLatin1String("enum")).toArray())
                values.append(v.toString());
            return QStringLiteral("one of: %1").arg(values.join(QStringLiteral(", ")));
        }
        return QStringLiteral("text");
    }
    if (type == QLatin1String("integer"))
        return 0;
    if (type == QLatin1String("number"))
        return 0.0;
    if (type == QLatin1String("boolean"))
        return true;
    return QJsonValue();
}

} // namespace

QString example(const QJsonObject &schema)
{
    const QJsonValue v = skeleton(schema);
    return QString::fromUtf8(v.isObject() ? QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented)
                                          : QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Indented))
        .trimmed();
}

} // namespace tumbler::ai
