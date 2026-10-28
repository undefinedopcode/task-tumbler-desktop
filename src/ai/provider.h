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
    QString provider; // "anthropic" / "ollama" / "claude-cli"
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

// Claude through the Claude Code CLI ("claude -p"), signed in however the
// user's organisation set it up, so no API key is needed. One turn, no
// tools, no MCP servers, nothing saved; the schema goes to --json-schema
// and the answer comes back on stdout.
class ClaudeCliProvider : public Provider
{
    Q_OBJECT
public:
    // An empty model means the CLI's own default.
    ClaudeCliProvider(const QString &program, const QString &model = QString(), QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("claude-cli"); }
    QString model() const override { return m_model; }
    void send(const Request &request, Callback done) override;

    // The arguments for a request (the prompt goes on stdin). Public for tests.
    QStringList arguments(const Request &request) const;

    // The claude executable: `configured` if it's set and runs, otherwise
    // the usual install places, which a GUI app's PATH often leaves out.
    // Empty when there's none.
    static QString locate(const QString &configured);

    void setTimeout(int ms) { m_timeoutMs = ms; } // tests

private:
    QString m_program;
    QString m_model;
    int m_timeoutMs = 5 * 60 * 1000;
};

} // namespace tumbler::ai
