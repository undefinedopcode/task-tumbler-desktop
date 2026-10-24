#include "aifeatures.h"

#include "jsonschema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <memory>

namespace tumbler::ai {

namespace {

const QString kWho = QStringLiteral(
    "You help someone with ADHD/ASD manage their day in Task Tumbler, a local task app. "
    "Be realistic rather than ambitious: a short list that gets done beats a long one that doesn't. "
    "Be kind and plain; never guilt-trip.");

QJsonObject str() { return {{QStringLiteral("type"), QStringLiteral("string")}}; }
QJsonObject integer() { return {{QStringLiteral("type"), QStringLiteral("integer")}}; }
QJsonObject arrayOf(const QJsonObject &items) { return {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), items}}; }
QJsonObject oneOf(const QStringList &values)
{
    return {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray::fromStringList(values)}};
}

// Every object closed and every property required: what structured output
// expects, and it keeps answers predictable.
QJsonObject object(const QList<QPair<QString, QJsonObject>> &props)
{
    QJsonObject properties;
    QJsonArray required;
    for (const auto &[name, schema] : props) {
        properties.insert(name, schema);
        required.append(name);
    }
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"), required},
            {QStringLiteral("additionalProperties"), false}};
}

QString compact(const QJsonValue &v)
{
    return QString::fromUtf8(v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented)
                                         : QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
}

QJsonObject taskJson(const Task &t, bool includeNotes, const QString &categoryText = {})
{
    QJsonObject o{
        {QStringLiteral("id"), t.id},
        {QStringLiteral("title"), t.title},
        {QStringLiteral("status"), nameOf(t.status)},
        {QStringLiteral("priority"), nameOf(t.priority)},
        {QStringLiteral("energy"), nameOf(t.energy)},
        {QStringLiteral("minutes"), t.estimateMinutes},
    };
    if (t.dueDate.isValid())
        o.insert(QStringLiteral("due"), t.dueDate.toString(Qt::ISODate));
    if (!t.tags.isEmpty())
        o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(t.tags));
    if (!categoryText.isEmpty())
        o.insert(QStringLiteral("category"), categoryText);
    if (includeNotes && !t.description.isEmpty())
        o.insert(QStringLiteral("notes"), t.description.left(500));
    return o;
}

Energy energyOf(const QString &s)
{
    return energyFromString(s).value_or(Energy::Medium);
}

} // namespace

void runValidated(Provider *provider, const Request &request, Callback done)
{
    auto attempt = std::make_shared<std::function<void(const Request &, int)>>();
    *attempt = [provider, done, attempt](const Request &req, int tries) {
        provider->send(req, [req, tries, done, attempt](const Result &r) {
            if (!r.ok) {
                // A malformed answer (not JSON, cut off) gets one more try.
                if (r.retryable && tries < 1) {
                    Request retry = req;
                    retry.prompt += QStringLiteral("\n\nYour previous answer wasn't usable (%1). Reply with only the JSON "
                                                   "object, matching the schema exactly, and keep text fields short.")
                                        .arg(r.error);
                    (*attempt)(retry, tries + 1);
                    return;
                }
                done(r);
                return;
            }
            const QString problem = validate(r.data, req.schema);
            if (problem.isEmpty()) {
                done(r);
                return;
            }
            if (tries >= 1) {
                Result failed = r;
                failed.ok = false;
                failed.error = QStringLiteral("the answer didn't have the expected shape (%1)").arg(problem);
                done(failed);
                return;
            }
            Request retry = req;
            retry.prompt += QStringLiteral("\n\nYour previous answer didn't match the required JSON schema: %1. "
                                           "Answer again with JSON that matches it exactly.")
                                .arg(problem);
            (*attempt)(retry, tries + 1);
        });
    };
    (*attempt)(request, 0);
}

// ---- plan ----------------------------------------------------------------------

Request planRequest(const PlanContext &c)
{
    QJsonArray tasks;
    for (const Task &t : c.tasks)
        tasks.append(taskJson(t, c.includeNotes, c.categoryHours.value(t.categoryId)));
    QJsonArray deps;
    for (const Dependency &d : c.dependencies)
        deps.append(QJsonObject{{QStringLiteral("task"), d.taskId}, {QStringLiteral("waits_on"), d.dependsOnId}});

    QStringList lines;
    lines << QStringLiteral("It is %1. Plan %2.").arg(c.nowText, c.day.toString(QStringLiteral("dddd d MMMM")));
    lines << (c.energy ? QStringLiteral("Their energy right now: %1/10.").arg(*c.energy)
                       : QStringLiteral("They haven't said how their energy is today."));
    if (!c.peakHours.isEmpty())
        lines << QStringLiteral("Their best hours are %1.").arg(c.peakHours);
    if (!c.fixedToday.isEmpty())
        lines << QStringLiteral("Already fixed today:\n- %1").arg(c.fixedToday.join(QStringLiteral("\n- ")));
    if (!c.userHints.trimmed().isEmpty())
        lines << QStringLiteral("Their own notes about scheduling:\n%1").arg(c.userHints.trimmed());
    lines << QStringLiteral("Open tasks:\n%1").arg(compact(tasks));
    if (!deps.isEmpty())
        lines << QStringLiteral("Dependencies (a task can't start until what it waits on is done):\n%1").arg(compact(deps));
    lines << QStringLiteral(
        "Decide what is worth doing that day and in what order. The app works out the actual times itself, fitting "
        "each task into its category's hours around fixed items, so don't give times.\n"
        "- order: task ids worth doing that day, most important first. Respect due dates and dependencies.\n"
        "- defer: tasks that should wait, each with a short, kind reason (e.g. needs more energy than they have, "
        "blocked by something outside the app, or the day is already full). Tasks in neither list are left for later.\n"
        "- notes: for some ordered tasks, a few words on why now (shown next to the block).\n"
        "- explanation: one or two sentences about the shape of the day.");

    Request r;
    r.feature = QStringLiteral("plan");
    r.system = kWho;
    r.prompt = lines.join(QStringLiteral("\n\n"));
    const QJsonObject idReason = object({{QStringLiteral("task_id"), str()}, {QStringLiteral("reason"), str()}});
    const QJsonObject idNote = object({{QStringLiteral("task_id"), str()}, {QStringLiteral("note"), str()}});
    r.schema = object({
        {QStringLiteral("order"), arrayOf(str())},
        {QStringLiteral("defer"), arrayOf(idReason)},
        {QStringLiteral("notes"), arrayOf(idNote)},
        {QStringLiteral("explanation"), str()},
    });
    return r;
}

PlanGuidance toGuidance(const QJsonObject &answer, const QSet<QString> &taskIds)
{
    PlanGuidance g;
    for (const QJsonValue &v : answer.value(QLatin1String("order")).toArray()) {
        const QString id = v.toString();
        if (taskIds.contains(id) && !g.order.contains(id))
            g.order.append(id);
    }
    for (const QJsonValue &v : answer.value(QLatin1String("defer")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QLatin1String("task_id")).toString();
        // Ordering wins if the model put a task in both lists.
        if (taskIds.contains(id) && !g.order.contains(id))
            g.defer.insert(id, o.value(QLatin1String("reason")).toString().left(200));
    }
    for (const QJsonValue &v : answer.value(QLatin1String("notes")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QLatin1String("task_id")).toString();
        if (taskIds.contains(id))
            g.notes.insert(id, o.value(QLatin1String("note")).toString().left(120));
    }
    g.explanation = answer.value(QLatin1String("explanation")).toString().left(600);
    return g;
}

// ---- breakdown -----------------------------------------------------------------

Request breakdownRequest(const Task &task, const QList<Task> &existingSteps, bool includeNotes, const QString &userHints)
{
    QStringList lines;
    lines << QStringLiteral("Break this task into small, concrete steps:\n%1").arg(compact(taskJson(task, includeNotes)));
    if (!existingSteps.isEmpty()) {
        QStringList titles;
        for (const Task &s : existingSteps)
            titles << s.title;
        lines << QStringLiteral("It already has these steps, so don't repeat them:\n- %1").arg(titles.join(QStringLiteral("\n- ")));
    }
    if (!userHints.trimmed().isEmpty())
        lines << QStringLiteral("Their own notes:\n%1").arg(userHints.trimmed());
    lines << QStringLiteral(
        "Make the first step tiny and obvious, so it's easy to start. Usually 3 to 7 steps, each 5 to 60 minutes, "
        "titled as an action. energy is how much focus the step needs (low, medium, high, peak). "
        "explanation is one sentence.");

    Request r;
    r.feature = QStringLiteral("breakdown");
    r.system = kWho;
    r.prompt = lines.join(QStringLiteral("\n\n"));
    r.schema = object({
        {QStringLiteral("steps"),
         arrayOf(object({{QStringLiteral("title"), str()},
                         {QStringLiteral("minutes"), integer()},
                         {QStringLiteral("energy"), oneOf({"low", "medium", "high", "peak"})}}))},
        {QStringLiteral("explanation"), str()},
    });
    return r;
}

QList<Step> toSteps(const QJsonObject &answer)
{
    QList<Step> steps;
    for (const QJsonValue &v : answer.value(QLatin1String("steps")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString title = o.value(QLatin1String("title")).toString().trimmed().left(200);
        if (title.isEmpty())
            continue;
        steps.append({title, std::clamp(o.value(QLatin1String("minutes")).toInt(15), 5, 480),
                      energyOf(o.value(QLatin1String("energy")).toString())});
        if (steps.size() == 12)
            break;
    }
    return steps;
}

// ---- capture -------------------------------------------------------------------

Request captureRequest(const QString &text, QDate today, const QStringList &categoryNames)
{
    QStringList lines;
    lines << QStringLiteral("Today is %1 (%2).").arg(today.toString(Qt::ISODate), today.toString(QStringLiteral("dddd")));
    if (!categoryNames.isEmpty())
        lines << QStringLiteral("Their categories: %1.").arg(categoryNames.join(QStringLiteral(", ")));
    lines << QStringLiteral("Turn this note into a task:\n\"%1\"").arg(text);
    lines << QStringLiteral(
        "- title: short and actionable, keeping their wording where possible.\n"
        "- date: YYYY-MM-DD if a day is mentioned or clearly implied, otherwise \"\".\n"
        "- time: HH:MM (24-hour) only if a specific time is mentioned, otherwise \"\".\n"
        "- estimate_minutes: a realistic guess.\n"
        "- category: one of their category names if it clearly fits, otherwise \"\".\n"
        "- recurrence: type none unless it repeats; days are 0=Sunday..6=Saturday for weekly.\n"
        "- priority and energy: medium unless the note suggests otherwise.");

    Request r;
    r.feature = QStringLiteral("capture");
    r.system = kWho;
    r.prompt = lines.join(QStringLiteral("\n\n"));
    r.maxTokens = 2000;
    r.schema = object({
        {QStringLiteral("title"), str()},
        {QStringLiteral("priority"), oneOf({"urgent", "high", "medium", "low"})},
        {QStringLiteral("energy"), oneOf({"low", "medium", "high", "peak"})},
        {QStringLiteral("estimate_minutes"), integer()},
        {QStringLiteral("date"), str()},
        {QStringLiteral("time"), str()},
        {QStringLiteral("tags"), arrayOf(str())},
        {QStringLiteral("category"), str()},
        {QStringLiteral("recurrence"),
         object({{QStringLiteral("type"), oneOf({"none", "daily", "weekly"})},
                 {QStringLiteral("interval"), integer()},
                 {QStringLiteral("days"), arrayOf(integer())}})},
    });
    return r;
}

CaptureResult toCapture(const QJsonObject &a, QDate today)
{
    CaptureResult r;
    r.title = a.value(QLatin1String("title")).toString().simplified().left(200);
    r.priority = priorityFromString(a.value(QLatin1String("priority")).toString());
    r.energy = energyFromString(a.value(QLatin1String("energy")).toString());
    const int minutes = a.value(QLatin1String("estimate_minutes")).toInt();
    if (minutes > 0)
        r.estimateMinutes = std::clamp(minutes, 5, 600);

    const QDate date = QDate::fromString(a.value(QLatin1String("date")).toString(), Qt::ISODate);
    // A date in the past is almost certainly a misreading; ignore it.
    if (date.isValid() && date >= today)
        r.date = date;
    const QTime time = QTime::fromString(a.value(QLatin1String("time")).toString(), QStringLiteral("HH:mm"));
    if (time.isValid())
        r.time = time;

    for (const QJsonValue &t : a.value(QLatin1String("tags")).toArray()) {
        const QString tag = t.toString().trimmed().remove(QLatin1Char('#'));
        if (!tag.isEmpty() && !r.tags.contains(tag, Qt::CaseInsensitive))
            r.tags.append(tag);
    }
    r.category = a.value(QLatin1String("category")).toString().trimmed();

    const QJsonObject rec = a.value(QLatin1String("recurrence")).toObject();
    const QString type = rec.value(QLatin1String("type")).toString();
    if (type == QLatin1String("daily") || type == QLatin1String("weekly")) {
        QJsonObject p{{QStringLiteral("type"), type},
                      {QStringLiteral("interval"), std::clamp(rec.value(QLatin1String("interval")).toInt(1), 1, 52)},
                      {QStringLiteral("startDate"), today.toString(Qt::ISODate)}};
        if (type == QLatin1String("weekly")) {
            QJsonArray days = rec.value(QLatin1String("days")).toArray();
            if (days.isEmpty())
                days.append(weekday0(today));
            p.insert(QStringLiteral("days"), days);
        }
        r.recurrence = RecurrencePattern::fromJson(p); // nullopt if the days were nonsense
    }
    return r;
}

} // namespace tumbler::ai
