#include "captureparser.h"

#include <QRegularExpression>

namespace tumbler {

namespace {

const QRegularExpression::PatternOptions kCi = QRegularExpression::CaseInsensitiveOption;

// 0 = Sunday, matching weekday0().
int weekdayFromName(const QString &name)
{
    static const char *const names[] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    const QString n = name.left(3).toLower();
    for (int i = 0; i < 7; ++i) {
        if (n == QLatin1String(names[i]))
            return i;
    }
    return -1;
}

const QString kDayName = QStringLiteral(
    R"((?:sun(?:day)?|mon(?:day)?|tue(?:s(?:day)?)?|wed(?:nesday)?|thu(?:r(?:s(?:day)?)?)?|fri(?:day)?|sat(?:urday)?))");

// Removes the match from `text`, returning true if anything matched.
bool take(QString &text, const QRegularExpression &re, QRegularExpressionMatch *out = nullptr)
{
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return false;
    if (out)
        *out = m;
    text.replace(m.capturedStart(), m.capturedLength(), QStringLiteral(" "));
    return true;
}

QList<int> daysIn(const QString &list)
{
    static const QRegularExpression dayRe(kDayName, kCi);
    QList<int> days;
    auto it = dayRe.globalMatch(list);
    while (it.hasNext()) {
        const int d = weekdayFromName(it.next().captured(0));
        if (d >= 0 && !days.contains(d))
            days.append(d);
    }
    std::sort(days.begin(), days.end());
    return days;
}

} // namespace

CaptureResult parseCapture(const QString &input, QDate today)
{
    CaptureResult r;
    QString text = QStringLiteral(" ") + input + QStringLiteral(" ");
    QRegularExpressionMatch m;

    // Recurrence first: "every monday" must not be read as a due date.
    const QString dayList = QStringLiteral(R"(%1(?:\s*(?:,|and|&)\s*%1)*)").arg(kDayName);
    RecurrencePattern p;
    p.startDate = today;
    if (take(text, QRegularExpression(QStringLiteral(R"(\s(?:daily|every\s+day)\s)"), kCi))) {
        p.type = RecurrencePattern::Type::Daily;
        r.recurrence = p;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\severy\s+(\d+)\s+days?\s)"), kCi), &m)) {
        p.type = RecurrencePattern::Type::Daily;
        p.interval = std::max(1, m.captured(1).toInt());
        r.recurrence = p;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\severy\s+weekdays?\s)"), kCi))) {
        p.type = RecurrencePattern::Type::Weekly;
        p.days = {1, 2, 3, 4, 5};
        r.recurrence = p;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\severy\s+weekends?\s)"), kCi))) {
        p.type = RecurrencePattern::Type::Weekly;
        p.days = {0, 6};
        r.recurrence = p;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\severy\s+(\d+)\s+weeks?\s+on\s+(%1)\s)").arg(dayList), kCi), &m)) {
        p.type = RecurrencePattern::Type::Weekly;
        p.interval = std::max(1, m.captured(1).toInt());
        p.days = daysIn(m.captured(2));
        r.recurrence = p;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\severy\s+other\s+(%1)\s)").arg(dayList), kCi), &m)) {
        p.type = RecurrencePattern::Type::Weekly;
        p.interval = 2;
        p.days = daysIn(m.captured(1));
        r.recurrence = p;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\s(?:every|weekly\s+on)\s+(%1)\s)").arg(dayList), kCi), &m)) {
        p.type = RecurrencePattern::Type::Weekly;
        p.days = daysIn(m.captured(1));
        r.recurrence = p;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\sweekly\s)"), kCi))) {
        p.type = RecurrencePattern::Type::Weekly;
        p.days = {weekday0(today)};
        r.recurrence = p;
    }

    // Time of day.
    if (take(text, QRegularExpression(QStringLiteral(R"(\s(?:at\s+|@)(\d{1,2})(?::(\d{2}))?\s*(am|pm)?\s)"), kCi), &m)) {
        int hour = m.captured(1).toInt();
        const int minute = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        const QString ampm = m.captured(3).toLower();
        if (ampm == QLatin1String("pm") && hour < 12)
            hour += 12;
        else if (ampm == QLatin1String("am") && hour == 12)
            hour = 0;
        const QTime t(hour, minute);
        if (t.isValid())
            r.time = t;
    }

    // Dates.
    if (take(text, QRegularExpression(QStringLiteral(R"(\s(\d{4}-\d{2}-\d{2})\s)")), &m)) {
        const QDate d = QDate::fromString(m.captured(1), Qt::ISODate);
        if (d.isValid())
            r.date = d;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\s(?:today|tonight)\s)"), kCi))) {
        r.date = today;
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\s(?:tomorrow|tmrw)\s)"), kCi))) {
        r.date = today.addDays(1);
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\sin\s+(\d+)\s+days?\s)"), kCi), &m)) {
        r.date = today.addDays(m.captured(1).toInt());
    } else if (take(text, QRegularExpression(QStringLiteral(R"(\snext\s+week\s)"), kCi))) {
        r.date = today.addDays(8 - today.dayOfWeek()); // next Monday
    } else if (take(text,
                    QRegularExpression(QStringLiteral(R"(\s(?:(?:on|next)\s+(%1)|(sunday|monday|tuesday|wednesday|thursday|friday|saturday))\s)")
                                           .arg(kDayName),
                                       kCi),
                    &m)) {
        // Bare day words must be spelled out ("sun cream" is not a date);
        // abbreviations need "on" or "next". The next such day, never today:
        // "fri" on a Friday means next week.
        const int target = weekdayFromName(m.captured(1).isEmpty() ? m.captured(2) : m.captured(1));
        int ahead = (target - weekday0(today) + 7) % 7;
        if (ahead == 0)
            ahead = 7;
        r.date = today.addDays(ahead);
    }

    // Estimate.
    if (take(text, QRegularExpression(QStringLiteral(R"(\s~(?:(\d+)h)?(?:(\d+)m?)?\s)"), kCi), &m)
        && (!m.captured(1).isEmpty() || !m.captured(2).isEmpty())) {
        const int minutes = m.captured(1).toInt() * 60 + m.captured(2).toInt();
        if (minutes > 0)
            r.estimateMinutes = minutes;
    }

    // Priority.
    if (take(text, QRegularExpression(QStringLiteral(R"(\s(!!!|!urgent)\s)"), kCi)))
        r.priority = Priority::Urgent;
    else if (take(text, QRegularExpression(QStringLiteral(R"(\s(!!|!high|!hi)\s)"), kCi)))
        r.priority = Priority::High;
    else if (take(text, QRegularExpression(QStringLiteral(R"(\s!med(?:ium)?\s)"), kCi)))
        r.priority = Priority::Medium;
    else if (take(text, QRegularExpression(QStringLiteral(R"(\s!lo(?:w)?\s)"), kCi)))
        r.priority = Priority::Low;

    // Energy.
    if (take(text, QRegularExpression(QStringLiteral(R"(\s\+(low|med(?:ium)?|high|peak)\s)"), kCi), &m)) {
        const QString e = m.captured(1).toLower();
        r.energy = e.startsWith(QLatin1String("med")) ? Energy::Medium : energyFromString(e).value_or(Energy::Medium);
    }

    // Category: the first /name.
    if (take(text, QRegularExpression(QStringLiteral(R"(\s/([\w-]+)\s)")), &m))
        r.category = m.captured(1);

    // Tags: every #tag.
    static const QRegularExpression tagRe(QStringLiteral(R"((?<=\s)#([\w:-]+)(?=\s))"));
    for (auto it = tagRe.globalMatch(text); it.hasNext();) {
        const QString tag = it.next().captured(1);
        if (!r.tags.contains(tag, Qt::CaseInsensitive))
            r.tags.append(tag);
    }
    text.replace(tagRe, QStringLiteral(" "));

    r.title = text.simplified();
    return r;
}

} // namespace tumbler
