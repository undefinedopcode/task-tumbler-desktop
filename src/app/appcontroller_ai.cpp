// AppController: planning, AI features, energy & health, events, AI settings.

#include "aifeatures.h"
#include "appcontroller.h"
#include "keystore.h"
#include "operations.h"
#include "provider.h"
#include "sync.h"
#include "variants.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSqlQuery>

namespace tumbler {

namespace {

constexpr qint64 kMinute = 60 * 1000;
const QString kApiKeyEntry = QStringLiteral("anthropic-api-key");

QString energyLabel(int level)
{
    if (level <= 2)
        return QStringLiteral("Wiped");
    if (level <= 4)
        return QStringLiteral("Low");
    if (level <= 6)
        return QStringLiteral("OK");
    if (level <= 8)
        return QStringLiteral("Good");
    return QStringLiteral("Great");
}

QString jsonText(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

std::optional<int> optInt(const QVariant &v)
{
    if (!v.isValid() || v.isNull() || v.toString().trimmed().isEmpty())
        return std::nullopt;
    bool ok = false;
    const int n = v.toString().toInt(&ok);
    return ok ? std::optional<int>(n) : std::nullopt;
}

} // namespace

void AppController::initAi()
{
    // The keyring is only opened once Claude is the chosen provider: opening
    // a closed KWallet prompts for its password, which nobody should see just
    // for starting the app (or for not using Claude at all).
    if (!m_keys || m_keyRequested)
        return;
    if (queries().setting(QStringLiteral("ai_provider"), QStringLiteral("off")).toString() != QLatin1String("anthropic"))
        return;
    m_keyRequested = true;
    m_keys->read(kApiKeyEntry, [this](const QString &value, const QString &error) {
        if (!error.isEmpty())
            qWarning() << "couldn't read the API key from the keyring:" << error;
        m_apiKey = value;
        Q_EMIT aiChanged();
    });
}

QVariantMap AppController::ai() const
{
    const Queries q = queries();
    const QString provider = q.setting(QStringLiteral("ai_provider"), QStringLiteral("off")).toString();
    const QString model = q.setting(QStringLiteral("ai_model"), QString::fromLatin1(ai::AnthropicProvider::kDefaultModel)).toString();
    const QString ollamaModel = q.setting(QStringLiteral("ollama_model"), QString()).toString();
    const bool ready = (provider == QLatin1String("anthropic") && !m_apiKey.isEmpty())
        || (provider == QLatin1String("ollama") && !ollamaModel.isEmpty());
    QString label;
    if (provider == QLatin1String("anthropic"))
        label = QStringLiteral("Claude (%1)").arg(model);
    else if (provider == QLatin1String("ollama"))
        label = ollamaModel.isEmpty() ? QStringLiteral("Ollama") : QStringLiteral("Ollama (%1)").arg(ollamaModel);
    return {
        {QStringLiteral("provider"), provider},
        {QStringLiteral("ready"), ready},
        {QStringLiteral("busy"), m_aiBusy},
        {QStringLiteral("label"), label},
        {QStringLiteral("hasKey"), !m_apiKey.isEmpty()},
        {QStringLiteral("model"), model},
        {QStringLiteral("ollamaUrl"), q.setting(QStringLiteral("ollama_url"), QStringLiteral("http://localhost:11434")).toString()},
        {QStringLiteral("ollamaModel"), ollamaModel},
        {QStringLiteral("shareNotes"), q.setting(QStringLiteral("ai_share_notes"), false).toBool()},
        {QStringLiteral("local"), provider == QLatin1String("ollama")},
    };
}

ai::Provider *AppController::provider()
{
    const QVariantMap a = ai();
    if (!a.value(QStringLiteral("ready")).toBool()) {
        m_provider.reset();
        m_providerSignature.clear();
        return nullptr;
    }
    const QString kind = a.value(QStringLiteral("provider")).toString();
    const QString signature = QStringList{kind, a.value(QStringLiteral("model")).toString(), a.value(QStringLiteral("ollamaUrl")).toString(),
                                          a.value(QStringLiteral("ollamaModel")).toString(),
                                          QString::fromLatin1(QCryptographicHash::hash(m_apiKey.toUtf8(), QCryptographicHash::Sha256).toHex())}
                                  .join(QLatin1Char('|'));
    if (signature != m_providerSignature || !m_provider) {
        if (kind == QLatin1String("anthropic"))
            m_provider = std::make_unique<ai::AnthropicProvider>(&m_network, m_apiKey, a.value(QStringLiteral("model")).toString());
        else
            m_provider = std::make_unique<ai::OllamaProvider>(&m_network, QUrl(a.value(QStringLiteral("ollamaUrl")).toString()),
                                                              a.value(QStringLiteral("ollamaModel")).toString());
        m_providerSignature = signature;
    }
    return m_provider.get();
}

void AppController::setAiBusy(bool busy)
{
    if (busy == m_aiBusy)
        return;
    m_aiBusy = busy;
    Q_EMIT aiChanged();
}

QString AppController::logAi(const QString &feature, const ai::Result &result, const QString &prompt)
{
    // Kept locally so it's possible to see later what was asked and answered.
    // Not undoable: it's a record, not data.
    const QString id = newId();
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("INSERT INTO ai_logs (id, action_type, provider, model, request_context, response, created_at) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(feature);
    q.addBindValue(result.provider);
    q.addBindValue(result.model);
    q.addBindValue(jsonText({{QStringLiteral("prompt"), prompt.left(20000)}}));
    q.addBindValue(jsonText(result.ok ? result.data
                                      : QJsonObject{{QStringLiteral("error"), result.error}}));
    q.addBindValue(now());
    if (!q.exec())
        qWarning() << "couldn't log AI call";
    return id;
}

void AppController::setAiFeedback(const QString &logId, const QString &feedback)
{
    if (logId.isEmpty())
        return;
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("UPDATE ai_logs SET user_feedback = ? WHERE id = ?"));
    q.addBindValue(feedback);
    q.addBindValue(logId);
    q.exec();
}

// ---- planning ---------------------------------------------------------------------

PlanOptions AppController::planOptions(QDate day) const
{
    const Queries q = queries();
    const QTimeZone z = zone();
    const QDate today = QDateTime::currentDateTime(z).date();
    PlanOptions o;
    o.day = day;
    o.earliest = day == today ? now() : QDateTime(day, QTime(0, 0), z).toMSecsSinceEpoch();
    const QTime peakStart = QTime::fromString(q.setting(QStringLiteral("peak_energy_start"), QStringLiteral("09:00")).toString(), QStringLiteral("HH:mm"));
    const QTime peakEnd = QTime::fromString(q.setting(QStringLiteral("peak_energy_end"), QStringLiteral("12:00")).toString(), QStringLiteral("HH:mm"));
    if (peakStart.isValid() && peakEnd.isValid() && peakStart < peakEnd)
        o.peak = TimeWindow{peakStart, peakEnd};
    o.breakMinutes = q.setting(QStringLiteral("break_duration_minutes"), 15).toInt();
    if (day == today) {
        if (const auto e = q.latestEnergy(QDateTime(today, QTime(0, 0), z).toMSecsSinceEpoch()))
            o.energyLevel = e->level;
    }
    o.pace = pace();
    return o;
}

stats::Pace AppController::pace() const
{
    const Queries q = queries();
    if (!q.setting(QStringLiteral("plan_with_pace"), true).toBool())
        return {};
    return stats::learnPace(q, now() - 90LL * 24 * 3600 * 1000, now() + 1);
}

int AppController::plannedMinutes(const Task &task) const
{
    const int estimate = std::max(5, task.estimateMinutes);
    return pace().plannedMinutes(estimate, task.categoryId);
}

QVariantMap AppController::planToVariant(const DayPlan &plan, const QString &aiError, const ai::Result *result, const QString &logId) const
{
    QVariantList blocks;
    for (const PlannedBlock &b : plan.blocks) {
        blocks.append(QVariantMap{{QStringLiteral("taskId"), b.taskId},
                                  {QStringLiteral("title"), b.title},
                                  {QStringLiteral("start"), b.start},
                                  {QStringLiteral("end"), b.end},
                                  {QStringLiteral("reason"), b.reason},
                                  {QStringLiteral("estimate"), b.estimateMinutes},
                                  {QStringLiteral("checked"), true}});
    }
    QVariantList unplaced;
    for (const UnplacedTask &u : plan.unplaced) {
        unplaced.append(QVariantMap{{QStringLiteral("taskId"), u.taskId}, {QStringLiteral("title"), u.title}, {QStringLiteral("reason"), u.reason}});
    }
    const bool usedAi = result && result->ok;
    return {
        {QStringLiteral("date"), plan.day.toString(Qt::ISODate)},
        {QStringLiteral("label"), QLocale().toString(plan.day, QStringLiteral("dddd d MMMM"))},
        {QStringLiteral("blocks"), blocks},
        {QStringLiteral("unplaced"), unplaced},
        {QStringLiteral("explanation"), plan.explanation},
        {QStringLiteral("usedAi"), usedAi},
        {QStringLiteral("aiError"), aiError},
        {QStringLiteral("by"), usedAi ? QStringLiteral("%1 · %2 s").arg(result->model).arg(result->elapsedMs / 1000.0, 0, 'f', 1)
                                      : QString()},
        {QStringLiteral("logId"), logId},
    };
}

void AppController::planDay(const QString &isoDate, bool useAi)
{
    const QDate day = QDate::fromString(isoDate, Qt::ISODate);
    if (!day.isValid())
        return;
    ensureOccurrences();
    PlanOptions options = planOptions(day);
    ai::Provider *p = useAi ? provider() : nullptr;
    if (!p) {
        Q_EMIT planReady(planToVariant(tumbler::planDay(queries(), options, zone()), {}, nullptr, {}));
        return;
    }

    // What the AI is told: the same candidates the planner would use, the
    // day's fixed items, energy and the user's own hints.
    const Queries q = queries();
    const QTimeZone z = zone();
    ai::PlanContext c;
    c.day = day;
    c.nowText = QLocale().toString(QDateTime::currentDateTime(z), QStringLiteral("dddd d MMMM, HH:mm"));
    c.energy = options.energyLevel;
    if (options.peak)
        c.peakHours = QStringLiteral("%1-%2").arg(options.peak->start.toString(QStringLiteral("HH:mm")), options.peak->end.toString(QStringLiteral("HH:mm")));
    c.userHints = q.setting(QStringLiteral("ai_scheduling_hints"), QString()).toString();
    c.includeNotes = ai().value(QStringLiteral("local")).toBool() || ai().value(QStringLiteral("shareNotes")).toBool();
    c.tasks = planCandidates(q, day, z);
    QSet<QString> ids;
    for (const Task &t : std::as_const(c.tasks))
        ids.insert(t.id);
    for (const Category &cat : q.categories()) {
        QStringList days;
        for (const QVariant &d : scheduleToVariant(cat.schedule)) {
            const QVariantMap m = d.toMap();
            if (m.value(QStringLiteral("enabled")).toBool())
                days.append(QStringLiteral("%1 %2-%3").arg(m.value(QStringLiteral("name")).toString(), m.value(QStringLiteral("start")).toString(),
                                                          m.value(QStringLiteral("end")).toString()));
        }
        c.categoryHours.insert(cat.id, QStringLiteral("%1 (%2)").arg(cat.name, days.isEmpty() ? QStringLiteral("no hours") : days.join(QStringLiteral(", "))));
    }
    for (const Dependency &d : q.dependencies()) {
        if (ids.contains(d.taskId))
            c.dependencies.append(d);
    }
    const qint64 dayStart = QDateTime(day, QTime(0, 0), z).toMSecsSinceEpoch();
    const qint64 dayEnd = QDateTime(day.addDays(1), QTime(0, 0), z).toMSecsSinceEpoch();
    const auto hm = [&z](qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms, z).toString(QStringLiteral("HH:mm")); };
    QHash<QString, QString> titles;
    for (const Task &t : q.tasks())
        titles.insert(t.id, t.title);
    for (const ScheduleBlock &s : q.schedules(dayStart, dayEnd)) {
        if (!s.completed)
            c.fixedToday.append(QStringLiteral("%1-%2 %3").arg(hm(s.start), hm(s.end), titles.value(s.taskId)));
    }
    for (const Event &e : q.events(dayStart, dayEnd, day, day)) {
        c.fixedToday.append(e.allDay ? QStringLiteral("all day: %1 (event)").arg(e.title)
                                     : QStringLiteral("%1-%2 %3 (event)").arg(hm(e.startAt), hm(e.endAt), e.title));
    }

    const ai::Request request = ai::planRequest(c);
    setAiBusy(true);
    ai::runValidated(p, request, [this, options, ids, request](const ai::Result &r) {
        setAiBusy(false);
        const QString logId = logAi(request.feature, r, request.prompt);
        PlanOptions o = options;
        if (r.ok)
            o.guidance = ai::toGuidance(r.data, ids);
        const DayPlan plan = tumbler::planDay(queries(), o, zone());
        Q_EMIT planReady(planToVariant(plan, r.ok ? QString() : r.error, &r, logId));
    });
}

bool AppController::acceptPlan(const QVariantList &blocks, const QVariantMap &plan)
{
    QList<Mutation> parts;
    for (const QVariant &v : blocks) {
        const QVariantMap b = v.toMap();
        parts.append(ops::scheduleTask(newId(), b.value(QStringLiteral("taskId")).toString(), b.value(QStringLiteral("start")).toLongLong(),
                                       b.value(QStringLiteral("end")).toLongLong(), now()));
    }
    const int offered = plan.value(QStringLiteral("blocks")).toList().size();
    if (parts.isEmpty()) {
        dismissPlan(plan);
        return false;
    }
    if (!apply(combine(QStringLiteral("Plan for %1").arg(plan.value(QStringLiteral("label")).toString()), parts)))
        return false;
    setAiFeedback(plan.value(QStringLiteral("logId")).toString(), parts.size() == offered ? QStringLiteral("accepted") : QStringLiteral("modified"));
    Q_EMIT message(QStringLiteral("Added %1 to the schedule (Ctrl+Z to undo)")
                       .arg(parts.size() == 1 ? QStringLiteral("1 block") : QStringLiteral("%1 blocks").arg(parts.size())));
    return true;
}

void AppController::dismissPlan(const QVariantMap &plan)
{
    setAiFeedback(plan.value(QStringLiteral("logId")).toString(), QStringLiteral("rejected"));
}

// ---- breakdown & capture -----------------------------------------------------------

void AppController::breakDown(const QString &taskId)
{
    ai::Provider *p = provider();
    const Queries q = queries();
    const auto task = q.task(taskId);
    if (!task)
        return;
    if (!p) {
        Q_EMIT aiFailed(QStringLiteral("breakdown"), QStringLiteral("Set up AI in Settings first"));
        Q_EMIT message(QStringLiteral("Set up AI in Settings first"));
        return;
    }
    QList<Task> steps;
    for (const Task &t : q.tasks()) {
        if (t.parentId == taskId)
            steps.append(t);
    }
    const bool includeNotes = ai().value(QStringLiteral("local")).toBool() || ai().value(QStringLiteral("shareNotes")).toBool();
    const ai::Request request = ai::breakdownRequest(*task, steps, includeNotes, q.setting(QStringLiteral("ai_scheduling_hints"), QString()).toString());
    setAiBusy(true);
    ai::runValidated(p, request, [this, taskId, request](const ai::Result &r) {
        setAiBusy(false);
        logAi(request.feature, r, request.prompt);
        if (!r.ok) {
            Q_EMIT aiFailed(request.feature, r.error);
            Q_EMIT message(QStringLiteral("Couldn't break it down: %1").arg(r.error));
            return;
        }
        QVariantList out;
        for (const ai::Step &s : ai::toSteps(r.data)) {
            out.append(QVariantMap{{QStringLiteral("title"), s.title},
                                   {QStringLiteral("minutes"), s.minutes},
                                   {QStringLiteral("energy"), nameOf(s.energy)},
                                   {QStringLiteral("checked"), true}});
        }
        Q_EMIT stepsReady(taskId, out, r.data.value(QLatin1String("explanation")).toString());
    });
}

bool AppController::addSteps(const QString &taskId, const QVariantList &steps)
{
    const auto parent = queries().task(taskId);
    if (!parent || steps.isEmpty())
        return false;
    QList<Mutation> parts;
    qint64 created = now();
    for (const QVariant &v : steps) {
        const QVariantMap s = v.toMap();
        Task t;
        t.id = newId();
        t.title = s.value(QStringLiteral("title")).toString().trimmed();
        if (t.title.isEmpty())
            continue;
        t.parentId = taskId;
        t.categoryId = parent->categoryId;
        t.estimateMinutes = std::max(5, s.value(QStringLiteral("minutes")).toInt());
        t.energy = energyFromString(s.value(QStringLiteral("energy")).toString()).value_or(Energy::Medium);
        t.priority = parent->priority;
        // One millisecond apart so the steps keep their order.
        parts.append(ops::createTask(t, created++));
    }
    if (parts.isEmpty())
        return false;
    return apply(combine(QStringLiteral("Add %1 steps to “%2”").arg(parts.size()).arg(parent->title), parts));
}

void AppController::aiCapture(const QString &text)
{
    ai::Provider *p = provider();
    if (!p || text.trimmed().isEmpty())
        return;
    QStringList categories;
    for (const Category &c : queries().categories())
        categories.append(c.name);
    const QDate today = QDateTime::currentDateTime(zone()).date();
    const ai::Request request = ai::captureRequest(text, today, categories);
    setAiBusy(true);
    ai::runValidated(p, request, [this, text, today, request](const ai::Result &r) {
        setAiBusy(false);
        logAi(request.feature, r, request.prompt);
        if (!r.ok) {
            Q_EMIT aiFailed(request.feature, r.error);
            return;
        }
        m_aiCaptureText = text;
        m_aiCaptureResult = ai::toCapture(r.data, today);
        Q_EMIT aiCaptureReady(text, previewCapture(text));
    });
}

// ---- energy & health ---------------------------------------------------------------

bool AppController::logEnergy(int level, const QString &notes)
{
    QString error;
    const bool ok = apply(ops::logEnergy(newId(), level, now(), notes, &error), error);
    if (ok)
        Q_EMIT message(QStringLiteral("Energy noted: %1 (%2/10)").arg(energyLabel(level)).arg(level));
    return ok;
}

bool AppController::saveHealth(const QVariantMap &v)
{
    HealthLog h;
    h.date = v.contains(QStringLiteral("date")) ? QDate::fromString(v.value(QStringLiteral("date")).toString(), Qt::ISODate)
                                                : QDateTime::currentDateTime(zone()).date();
    h.sleepScore = optInt(v.value(QStringLiteral("sleep")));
    h.readinessScore = optInt(v.value(QStringLiteral("readiness")));
    const QString hours = v.value(QStringLiteral("hours")).toString().trimmed();
    if (!hours.isEmpty()) {
        // Accept "7,5" in locales that write it that way, and "7.5" anywhere.
        bool ok = false;
        double hrs = QLocale().toDouble(hours, &ok);
        if (!ok)
            hrs = hours.toDouble(&ok);
        if (ok)
            h.hoursSlept = hrs;
    }
    h.notes = v.value(QStringLiteral("notes")).toString();
    QString error;
    return apply(ops::saveHealth(queries(), h, now(), &error), error);
}

QVariantMap AppController::healthToday() const
{
    const auto h = queries().healthLog(QDateTime::currentDateTime(zone()).date());
    if (!h)
        return {};
    return {
        {QStringLiteral("sleep"), h->sleepScore ? QVariant(*h->sleepScore) : QVariant()},
        {QStringLiteral("readiness"), h->readinessScore ? QVariant(*h->readinessScore) : QVariant()},
        {QStringLiteral("hours"), h->hoursSlept ? QVariant(*h->hoursSlept) : QVariant()},
        {QStringLiteral("notes"), h->notes},
    };
}

QVariantMap AppController::energyHistory(int days) const
{
    const Queries q = queries();
    const QTimeZone z = zone();
    const QDate last = QDateTime::currentDateTime(z).date();
    const QDate first = last.addDays(1 - std::max(1, days));
    const qint64 from = QDateTime(first, QTime(0, 0), z).toMSecsSinceEpoch();
    const qint64 to = QDateTime(last.addDays(1), QTime(0, 0), z).toMSecsSinceEpoch();

    QVariantList energy;
    QHash<QDate, QPair<int, int>> perDay; // sum, count
    for (const EnergyLog &e : q.energyLogs(from, to)) {
        energy.append(QVariantMap{{QStringLiteral("at"), e.loggedAt}, {QStringLiteral("level"), e.level}, {QStringLiteral("label"), energyLabel(e.level)}});
        auto &d = perDay[QDateTime::fromMSecsSinceEpoch(e.loggedAt, z).date()];
        d.first += e.level;
        ++d.second;
    }
    QHash<QDate, HealthLog> health;
    for (const HealthLog &h : q.healthLogs(first, last))
        health.insert(h.date, h);

    QVariantList out;
    for (QDate d = first; d <= last; d = d.addDays(1)) {
        const auto e = perDay.value(d);
        const HealthLog h = health.value(d);
        out.append(QVariantMap{
            {QStringLiteral("date"), d.toString(Qt::ISODate)},
            {QStringLiteral("label"), QLocale().toString(d, QStringLiteral("ddd d"))},
            {QStringLiteral("energy"), e.second ? QVariant(double(e.first) / e.second) : QVariant()},
            {QStringLiteral("sleep"), h.sleepScore ? QVariant(*h.sleepScore) : QVariant()},
            {QStringLiteral("readiness"), h.readinessScore ? QVariant(*h.readinessScore) : QVariant()},
            {QStringLiteral("hours"), h.hoursSlept ? QVariant(*h.hoursSlept) : QVariant()},
        });
    }
    return {{QStringLiteral("energy"), energy}, {QStringLiteral("days"), out}};
}

void AppController::refreshEnergy()
{
    const QTimeZone z = zone();
    const QDateTime nowLocal = QDateTime::currentDateTime(z);
    const qint64 midnight = QDateTime(nowLocal.date(), QTime(0, 0), z).toMSecsSinceEpoch();
    QVariantMap e;
    const auto latest = queries().latestEnergy(midnight);
    if (latest) {
        e = QVariantMap{{QStringLiteral("level"), latest->level},
                        {QStringLiteral("at"), latest->loggedAt},
                        {QStringLiteral("label"), energyLabel(latest->level)}};
    }
    // Ask again after four quiet hours, but only during waking hours.
    const bool stale = !latest || now() - latest->loggedAt > 4 * 60 * kMinute;
    e.insert(QStringLiteral("due"), stale && nowLocal.time().hour() >= 7 && nowLocal.time().hour() < 21);
    if (e != m_energy) {
        m_energy = e;
        Q_EMIT energyChanged();
    }
}

// ---- events -----------------------------------------------------------------------

QVariantMap AppController::eventDetail(const QString &id) const
{
    const auto e = queries().event(id);
    if (!e)
        return {};
    const QTimeZone z = zone();
    const QDateTime start = QDateTime::fromMSecsSinceEpoch(e->startAt, z);
    const QDateTime end = QDateTime::fromMSecsSinceEpoch(e->endAt, z);
    return {
        {QStringLiteral("id"), e->id},
        {QStringLiteral("title"), e->title},
        {QStringLiteral("description"), e->description},
        {QStringLiteral("allDay"), e->allDay},
        {QStringLiteral("date"), (e->allDay ? e->startDate : start.date()).toString(Qt::ISODate)},
        {QStringLiteral("endDate"), (e->allDay ? e->endDate : end.date()).toString(Qt::ISODate)},
        {QStringLiteral("startTime"), e->allDay ? QStringLiteral("09:00") : start.toString(QStringLiteral("HH:mm"))},
        {QStringLiteral("endTime"), e->allDay ? QStringLiteral("10:00") : end.toString(QStringLiteral("HH:mm"))},
        {QStringLiteral("blockingMode"), nameOf(e->blocking.mode)},
        {QStringLiteral("allowedTags"), e->blocking.allowedTags},
        {QStringLiteral("color"), e->color},
        {QStringLiteral("location"), e->location},
        // From a subscribed calendar: read-only here, changed at the source.
        {QStringLiteral("feed"), feedNameOf(e->id)},
    };
}

QString AppController::feedNameOf(const QString &eventId) const
{
    const QString feedId = sync::feedOf(queries(), eventId);
    if (feedId.isEmpty())
        return {};
    for (const QJsonValue &f : queries().setting(QStringLiteral("calendar_feeds"), QJsonArray()).toArray()) {
        if (f.toObject().value(QLatin1String("id")).toString() == feedId)
            return f.toObject().value(QLatin1String("name")).toString();
    }
    return QStringLiteral("a subscribed calendar");
}

QString AppController::saveEvent(const QVariantMap &v)
{
    Event e;
    e.id = v.value(QStringLiteral("id")).toString();
    if (const QString feed = e.id.isEmpty() ? QString() : feedNameOf(e.id); !feed.isEmpty()) {
        Q_EMIT message(QStringLiteral("That event comes from “%1”; change it there").arg(feed));
        return {};
    }
    const bool isNew = e.id.isEmpty();
    if (isNew)
        e.id = newId();
    e.title = v.value(QStringLiteral("title")).toString();
    e.description = v.value(QStringLiteral("description")).toString();
    e.allDay = v.value(QStringLiteral("allDay")).toBool();
    e.color = v.value(QStringLiteral("color")).toString();
    e.location = v.value(QStringLiteral("location")).toString();
    e.blocking.mode = blockingModeFromString(v.value(QStringLiteral("blockingMode")).toString()).value_or(BlockingMode::None);
    e.blocking.allowedTags = v.value(QStringLiteral("allowedTags")).toStringList();

    const QDate date = QDate::fromString(v.value(QStringLiteral("date")).toString(), Qt::ISODate);
    QDate endDate = QDate::fromString(v.value(QStringLiteral("endDate")).toString(), Qt::ISODate);
    if (!endDate.isValid() || endDate < date)
        endDate = date;
    if (e.allDay) {
        e.startDate = date;
        e.endDate = endDate;
    } else {
        const QTime st = QTime::fromString(v.value(QStringLiteral("startTime")).toString(), QStringLiteral("HH:mm"));
        const QTime et = QTime::fromString(v.value(QStringLiteral("endTime")).toString(), QStringLiteral("HH:mm"));
        if (!date.isValid() || !st.isValid() || !et.isValid()) {
            Q_EMIT message(QStringLiteral("Give the event a date and start and end times"));
            return {};
        }
        e.startAt = QDateTime(date, st, zone()).toMSecsSinceEpoch();
        // An end time earlier than the start on the same day means it runs past midnight.
        QDate endDay = endDate;
        if (endDay == date && et <= st)
            endDay = date.addDays(1);
        e.endAt = QDateTime(endDay, et, zone()).toMSecsSinceEpoch();
    }
    QString error;
    if (!apply(ops::saveEvent(e, isNew, QString::fromUtf8(zone().id()), now(), &error), error))
        return {};
    return e.id;
}

bool AppController::deleteEvent(const QString &id)
{
    const auto e = queries().event(id);
    if (!e)
        return false;
    if (const QString feed = feedNameOf(id); !feed.isEmpty()) {
        Q_EMIT message(QStringLiteral("That event comes from “%1”; remove it there").arg(feed));
        return false;
    }
    Mutation m = ops::deleteEvent(id);
    m.text = QStringLiteral("Delete event “%1”").arg(e->title);
    const bool ok = apply(m);
    if (ok)
        Q_EMIT message(QStringLiteral("Deleted “%1” (Ctrl+Z to undo)").arg(e->title));
    return ok;
}

// ---- AI settings ------------------------------------------------------------------

void AppController::setApiKey(const QString &key)
{
    m_keyRequested = true; // a pending read must not overwrite this
    m_apiKey = key.trimmed();
    Q_EMIT aiChanged();
    if (!m_keys)
        return;
    m_keys->write(kApiKeyEntry, m_apiKey, [this](const QString &error) {
        Q_EMIT message(error.isEmpty() ? (m_apiKey.isEmpty() ? QStringLiteral("API key removed from your keyring")
                                                             : QStringLiteral("API key saved to your keyring"))
                                       : QStringLiteral("Couldn't save the key to the keyring: %1").arg(error));
    });
}

void AppController::testAi()
{
    ai::Provider *p = provider();
    if (!p) {
        Q_EMIT message(QStringLiteral("Choose a provider and finish setting it up first"));
        return;
    }
    ai::Request r;
    r.feature = QStringLiteral("test");
    r.system = QStringLiteral("You are a connectivity check.");
    r.prompt = QStringLiteral("Reply with ok set to true.");
    r.maxTokens = 2000;
    r.schema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                           {QStringLiteral("properties"), QJsonObject{{QStringLiteral("ok"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
                           {QStringLiteral("required"), QJsonArray{QStringLiteral("ok")}},
                           {QStringLiteral("additionalProperties"), false}};
    setAiBusy(true);
    ai::runValidated(p, r, [this](const ai::Result &res) {
        setAiBusy(false);
        Q_EMIT message(res.ok ? QStringLiteral("Connected: %1 answered in %2 s").arg(res.model).arg(res.elapsedMs / 1000.0, 0, 'f', 1)
                              : QStringLiteral("Not working yet: %1").arg(res.error));
    });
}

void AppController::refreshOllamaModels()
{
    const QUrl url(ai().value(QStringLiteral("ollamaUrl")).toString());
    ai::OllamaProvider::listModels(&m_network, url, [this](const QStringList &models, const QString &error) {
        if (!error.isEmpty())
            Q_EMIT message(QStringLiteral("Couldn't list Ollama models: %1").arg(error));
        m_ollamaModels = models;
        Q_EMIT ollamaModelsChanged();
    });
}

} // namespace tumbler
