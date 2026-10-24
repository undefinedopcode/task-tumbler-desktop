#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace tumbler::ai {

// One request: instructions, the user's content, and the JSON Schema the
// answer must match. Every feature asks for structured output, so callers
// always get an object back (or an error), never prose to parse.
struct Request {
    QString feature; // "plan", "breakdown", "capture" (for logs)
    QString system;
    QString prompt;
    QJsonObject schema;
    int maxTokens = 8000;
};

struct Result {
    bool ok = false;
    QJsonObject data;
    QString error;    // for the user: short and plain
    bool retryable = false; // malformed answer: worth asking once more
    QString provider; // "anthropic" / "ollama"
    QString model;
    qint64 elapsedMs = 0;
};

using Callback = std::function<void(const Result &)>;

// The JSON object in a model's answer. Tolerates what local models wrap
// around it despite being asked not to: <think> blocks, ``` fences, a
// sentence before or after. Empty with `error` set when there's no object.
QJsonObject extractJsonObject(const QString &text, QString *error);

class Provider : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual QString id() const = 0;
    virtual QString model() const = 0;
    // Answers once, on the event loop. Never blocks.
    virtual void send(const Request &request, Callback done) = 0;
};

// Claude via the Messages API. Structured output through
// output_config.format; server-side fallbacks on, so a request one model
// declines is retried on another rather than failing.
class AnthropicProvider : public Provider
{
    Q_OBJECT
public:
    static constexpr const char *kDefaultModel = "claude-opus-5-5";

    AnthropicProvider(QNetworkAccessManager *network, const QString &apiKey, const QString &model = QString::fromLatin1(kDefaultModel),
                      QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("anthropic"); }
    QString model() const override { return m_model; }
    void send(const Request &request, Callback done) override;

    void setBaseUrl(const QUrl &url) { m_baseUrl = url; } // tests
    void setEffort(const QString &effort) { m_effort = effort; }

    // The JSON body sent for a request. Public for tests.
    QJsonObject body(const Request &request) const;

private:
    QNetworkAccessManager *m_network;
    QString m_apiKey;
    QString m_model;
    QString m_effort = QStringLiteral("medium");
    QUrl m_baseUrl = QUrl(QStringLiteral("https://api.anthropic.com"));
};

// A local model through Ollama's /api/chat, with the schema passed as
// `format` so the model is constrained to it.
class OllamaProvider : public Provider
{
    Q_OBJECT
public:
    OllamaProvider(QNetworkAccessManager *network, const QUrl &baseUrl, const QString &model, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("ollama"); }
    QString model() const override { return m_model; }
    void send(const Request &request, Callback done) override;

    QJsonObject body(const Request &request) const;

    // Installed model names, for the Settings picker.
    static void listModels(QNetworkAccessManager *network, const QUrl &baseUrl,
                           std::function<void(const QStringList &models, const QString &error)> done);

private:
    QNetworkAccessManager *m_network;
    QUrl m_baseUrl;
    QString m_model;
};

} // namespace tumbler::ai
