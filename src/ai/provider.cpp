#include "provider.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <optional>

namespace tumbler::ai {

namespace {

constexpr int kTimeoutMs = 180 * 1000;

// HTTP and API errors as one short sentence a person can act on.
QString describeFailure(QNetworkReply *reply, const QJsonObject &body, const QString &service)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString apiMessage = body.value(QLatin1String("error")).isObject()
        ? body.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString()
        : body.value(QLatin1String("error")).toString();
    if (status == 401 || status == 403)
        return QStringLiteral("%1 rejected the API key").arg(service);
    if (status == 429)
        return QStringLiteral("%1 is rate limiting; try again in a minute").arg(service);
    if (status >= 500)
        return QStringLiteral("%1 is having trouble right now (%2)").arg(service).arg(status);
    if (!apiMessage.isEmpty())
        return QStringLiteral("%1: %2").arg(service, apiMessage);
    if (reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError)
        return QStringLiteral("%1 took too long to answer").arg(service);
    if (status == 0)
        return QStringLiteral("couldn't reach %1 (%2)").arg(service, reply->errorString());
    return QStringLiteral("%1 answered with an error (%2)").arg(service).arg(status);
}

} // namespace

QJsonObject extractJsonObject(const QString &text, QString *error)
{
    const auto tryParse = [](const QString &candidate) -> std::optional<QJsonObject> {
        const QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8());
        if (doc.isObject())
            return doc.object();
        return std::nullopt;
    };

    QString t = text;
    // Reasoning some models print before the answer.
    static const QRegularExpression think(QStringLiteral(R"(<think>.*?</think>)"), QRegularExpression::DotMatchesEverythingOption);
    t.remove(think);
    t = t.trimmed();
    if (const auto o = tryParse(t))
        return *o;

    // ```json ... ``` fences.
    static const QRegularExpression fence(QStringLiteral(R"(```(?:json)?\s*(.*?)```)"), QRegularExpression::DotMatchesEverythingOption);
    if (const auto m = fence.match(t); m.hasMatch()) {
        if (const auto o = tryParse(m.captured(1).trimmed()))
            return *o;
    }

    // The first balanced {...}, minding braces inside strings.
    const qsizetype open = t.indexOf(QLatin1Char('{'));
    if (open >= 0) {
        int depth = 0;
        bool inString = false, escaped = false;
        for (qsizetype i = open; i < t.size(); ++i) {
            const QChar c = t.at(i);
            if (inString) {
                if (escaped)
                    escaped = false;
                else if (c == QLatin1Char('\\'))
                    escaped = true;
                else if (c == QLatin1Char('"'))
                    inString = false;
                continue;
            }
            if (c == QLatin1Char('"'))
                inString = true;
            else if (c == QLatin1Char('{'))
                ++depth;
            else if (c == QLatin1Char('}') && --depth == 0) {
                if (const auto o = tryParse(t.mid(open, i - open + 1)))
                    return *o;
                break;
            }
        }
    }

    QJsonParseError parseError;
    QJsonDocument::fromJson(t.toUtf8(), &parseError);
    *error = QStringLiteral("the answer wasn't valid JSON (%1)").arg(parseError.error == QJsonParseError::NoError
                                                                         ? QStringLiteral("not an object")
                                                                         : parseError.errorString());
    return {};
}

// ---- Anthropic ---------------------------------------------------------------

AnthropicProvider::AnthropicProvider(QNetworkAccessManager *network, const QString &apiKey, const QString &model, QObject *parent)
    : Provider(parent)
    , m_network(network)
    , m_apiKey(apiKey)
    , m_model(model.isEmpty() ? QString::fromLatin1(kDefaultModel) : model)
{
}

QJsonObject AnthropicProvider::body(const Request &request) const
{
    return QJsonObject{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("max_tokens"), request.maxTokens},
        {QStringLiteral("system"), request.system},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), request.prompt}}}},
        {QStringLiteral("output_config"),
         QJsonObject{
             {QStringLiteral("effort"), m_effort},
             {QStringLiteral("format"),
              QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")}, {QStringLiteral("schema"), request.schema}}},
         }},
        // A declined request is re-run on Anthropic's recommended fallback
        // model, chosen by why it was declined.
        {QStringLiteral("fallbacks"), QStringLiteral("default")},
    };
}

void AnthropicProvider::send(const Request &request, Callback done)
{
    QUrl url = m_baseUrl;
    url.setPath(QStringLiteral("/v1/messages"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("x-api-key", m_apiKey.toUtf8());
    req.setRawHeader("anthropic-version", "2023-06-01");
    req.setRawHeader("anthropic-beta", "server-side-fallback-2026-07-01");
    req.setTransferTimeout(kTimeoutMs);

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    QNetworkReply *reply = m_network->post(req, QJsonDocument(body(request)).toJson(QJsonDocument::Compact));
    const QString model = m_model;
    connect(reply, &QNetworkReply::finished, this, [reply, done, timer, model] {
        reply->deleteLater();
        Result r;
        r.provider = QStringLiteral("anthropic");
        r.model = model;
        r.elapsedMs = timer->elapsed();

        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || response.value(QLatin1String("type")) == QLatin1String("error")) {
            r.error = describeFailure(reply, response, QStringLiteral("Claude"));
            done(r);
            return;
        }
        if (!response.value(QLatin1String("model")).toString().isEmpty())
            r.model = response.value(QLatin1String("model")).toString();

        // Check why it stopped before reading anything.
        const QString stop = response.value(QLatin1String("stop_reason")).toString();
        if (stop == QLatin1String("refusal")) {
            r.error = QStringLiteral("Claude declined this request");
            done(r);
            return;
        }
        if (stop == QLatin1String("max_tokens")) {
            r.error = QStringLiteral("the answer was cut off before it finished");
            done(r);
            return;
        }

        // With structured output the answer is the first text block; thinking
        // blocks may come before it.
        for (const QJsonValue &block : response.value(QLatin1String("content")).toArray()) {
            const QJsonObject b = block.toObject();
            if (b.value(QLatin1String("type")) == QLatin1String("text")) {
                r.data = extractJsonObject(b.value(QLatin1String("text")).toString(), &r.error);
                r.ok = r.error.isEmpty();
                r.retryable = !r.ok;
                done(r);
                return;
            }
        }
        r.error = QStringLiteral("Claude's answer had no text in it");
        done(r);
    });
}

// ---- Ollama ------------------------------------------------------------------

OllamaProvider::OllamaProvider(QNetworkAccessManager *network, const QUrl &baseUrl, const QString &model, QObject *parent)
    : Provider(parent)
    , m_network(network)
    , m_baseUrl(baseUrl)
    , m_model(model)
{
}

QJsonObject OllamaProvider::body(const Request &request) const
{
    return QJsonObject{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("stream"), false},
        {QStringLiteral("format"), request.schema},
        {QStringLiteral("messages"),
         QJsonArray{
             QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), request.system}},
             QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), request.prompt}},
         }},
        {QStringLiteral("options"),
         QJsonObject{
             // Low temperature: planning and extraction, not prose.
             {QStringLiteral("temperature"), 0.2},
             // Ollama's default context is small enough that a plan prompt
             // gets silently truncated, and the answer turns to junk.
             {QStringLiteral("num_ctx"), 16384},
             // And its default output cap can cut the JSON off midway.
             {QStringLiteral("num_predict"), request.maxTokens},
         }},
    };
}

void OllamaProvider::send(const Request &request, Callback done)
{
    QUrl url = m_baseUrl;
    url.setPath(QStringLiteral("/api/chat"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(kTimeoutMs);

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    QNetworkReply *reply = m_network->post(req, QJsonDocument(body(request)).toJson(QJsonDocument::Compact));
    const QString model = m_model;
    connect(reply, &QNetworkReply::finished, this, [reply, done, timer, model] {
        reply->deleteLater();
        Result r;
        r.provider = QStringLiteral("ollama");
        r.model = model;
        r.elapsedMs = timer->elapsed();

        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || response.contains(QLatin1String("error"))) {
            r.error = describeFailure(reply, response, QStringLiteral("Ollama"));
            done(r);
            return;
        }
        // Hitting the output limit leaves the JSON unfinished.
        if (response.value(QLatin1String("done_reason")).toString() == QLatin1String("length")) {
            r.error = QStringLiteral("the answer was cut off before it finished");
            r.retryable = true;
            done(r);
            return;
        }
        const QString content = response.value(QLatin1String("message")).toObject().value(QLatin1String("content")).toString();
        r.data = extractJsonObject(content, &r.error);
        r.ok = r.error.isEmpty();
        r.retryable = !r.ok;
        done(r);
    });
}

void OllamaProvider::listModels(QNetworkAccessManager *network, const QUrl &baseUrl,
                                std::function<void(const QStringList &, const QString &)> done)
{
    QUrl url = baseUrl;
    url.setPath(QStringLiteral("/api/tags"));
    QNetworkRequest req(url);
    req.setTransferTimeout(5000);
    QNetworkReply *reply = network->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done] {
        reply->deleteLater();
        const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError) {
            done({}, describeFailure(reply, response, QStringLiteral("Ollama")));
            return;
        }
        QStringList models;
        for (const QJsonValue &m : response.value(QLatin1String("models")).toArray())
            models.append(m.toObject().value(QLatin1String("name")).toString());
        models.sort();
        done(models, {});
    });
}

} // namespace tumbler::ai
