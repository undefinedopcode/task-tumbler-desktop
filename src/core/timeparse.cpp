#include "timeparse.h"

#include <QRegularExpression>

namespace tumbler {

std::optional<ParsedTimestamp> parseTimestamp(const QString &text, const QTimeZone &naiveZone)
{
    static const QRegularExpression re(QStringLiteral(
        R"(^(\d{4}-\d{2}-\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:\.(\d+))?)?)?\s*(Z|[+-]\d{2}(?::?\d{2})?)?$)"));

    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return std::nullopt;

    const QDate date = QDate::fromString(m.captured(1), Qt::ISODate);
    if (!date.isValid())
        return std::nullopt;

    QTime time(0, 0);
    if (m.hasCaptured(2)) {
        // Fractions come with 3 (JS) or 6 (Postgres) digits; keep milliseconds.
        const int ms = m.hasCaptured(5) ? m.captured(5).left(3).leftJustified(3, QLatin1Char('0')).toInt() : 0;
        time = QTime(m.captured(2).toInt(), m.captured(3).toInt(), m.hasCaptured(4) ? m.captured(4).toInt() : 0, ms);
        if (!time.isValid())
            return std::nullopt;
    }

    ParsedTimestamp result;
    const QString zone = m.captured(6);
    if (zone.isEmpty()) {
        result.utc = QDateTime(date, time, naiveZone).toUTC();
    } else {
        int offsetSeconds = 0;
        if (zone != QLatin1String("Z")) {
            const QString digits = QString(zone.mid(1)).remove(QLatin1Char(':'));
            const int hours = digits.left(2).toInt();
            const int minutes = digits.size() >= 4 ? digits.mid(2, 2).toInt() : 0;
            offsetSeconds = (hours * 3600 + minutes * 60) * (zone.startsWith(QLatin1Char('-')) ? -1 : 1);
        }
        result.utc = QDateTime(date, time, QTimeZone::fromSecondsAheadOfUtc(offsetSeconds)).toUTC();
        result.hadOffset = true;
    }

    if (!result.utc.isValid())
        return std::nullopt;
    return result;
}

std::optional<qint64> parseTimestampMs(const QString &text, const QTimeZone &naiveZone)
{
    const auto parsed = parseTimestamp(text, naiveZone);
    if (!parsed)
        return std::nullopt;
    return parsed->utc.toMSecsSinceEpoch();
}

QDate parseLocalDate(const QString &text, const QTimeZone &zone)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() == 10)
        return QDate::fromString(trimmed, Qt::ISODate);

    const auto parsed = parseTimestamp(trimmed, zone);
    if (!parsed)
        return {};
    if (parsed->hadOffset)
        return parsed->utc.toTimeZone(zone).date();
    return QDate::fromString(trimmed.left(10), Qt::ISODate);
}

} // namespace tumbler
