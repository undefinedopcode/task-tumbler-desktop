#include "mcpserver.h"

#include <QJsonDocument>
#include <cstdio>
#include <iostream>
#include <string>

namespace tumbler::mcp {

namespace {

QJsonObject prop(const char *type, const QString &description)
{
    return {{QStringLiteral("type"), QLatin1String(type)}, {QStringLiteral("description"), description}};
}

QJsonObject tool(const QString &name, const QString &description, const QJsonObject &properties = {}, const QStringList &required = {},
                 bool readOnly = false)
{
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("inputSchema"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                     {QStringLiteral("properties"), properties},
                     {QStringLiteral("required"), QJsonArray::fromStringList(required)}}},
        {QStringLiteral("annotations"), QJsonObject{{QStringLiteral("readOnlyHint"), readOnly}}},
    };
}

QJsonObject result(const QJsonValue &id, const QJsonObject &payload)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), payload}};
}

QJsonObject failure(const QJsonValue &id, int code, const QString &message)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject toolText(const QString &text, bool isError = false)
{
    return {{QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}}}},
            {QStringLiteral("isError"), isError}};
}

} // namespace

Server::Server(Backend *backend)
    : m_backend(backend)
{
}

QStringList Server::supportedVersions()
{
    return {QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
}

QJsonArray Server::tools()
{
    const QJsonObject id{{QStringLiteral("id"), prop("string", QStringLiteral("Task id, from list_tasks or get_today"))}};
    return {
        tool(QStringLiteral("get_today"),
             QStringLiteral("Today's agenda: what's on now, what was missed, what's coming up, what's being worked on, and up to "
                            "eight open tasks ready to start."),
             {}, {}, true),
        tool(QStringLiteral("list_tasks"), QStringLiteral("Tasks with their status, priority, energy, estimate, due date, tags and category."),
             {{QStringLiteral("filter"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                           {QStringLiteral("enum"), QJsonArray{QStringLiteral("open"), QStringLiteral("done"), QStringLiteral("all")}},
                           {QStringLiteral("description"), QStringLiteral("Which tasks; default open")}}},
              {QStringLiteral("search"), prop("string", QStringLiteral("Only tasks whose title or tags contain this"))}},
             {}, true),
        tool(QStringLiteral("get_task"), QStringLiteral("One task with its steps, what it waits on, and its scheduled blocks."), id,
             {QStringLiteral("id")}, true),
        tool(QStringLiteral("get_current"), QStringLiteral("The task being worked on right now and when the session started, if any."), {}, {},
             true),
        tool(QStringLiteral("add_task"),
             QStringLiteral("Capture a task from one line of text. Understands: tomorrow, friday, 2026-11-03, 'at 3pm', ~30m (estimate), "
                            "!urgent !high !low, +low/+high/+peak (energy), #tag, /category, and repeats like 'every mon and thu' or "
                            "'daily'. A time of day also puts it on the schedule. Returns the new task's id."),
             {{QStringLiteral("text"), prop("string", QStringLiteral("e.g. 'review the PR friday ~45m #work !high'"))}},
             {QStringLiteral("text")}),
        tool(QStringLiteral("complete_task"), QStringLiteral("Mark a task done."), id, {QStringLiteral("id")}),
        tool(QStringLiteral("start_work"), QStringLiteral("Start the work timer on a task (stops any other)."), id, {QStringLiteral("id")}),
        tool(QStringLiteral("stop_work"), QStringLiteral("Stop the work timer.")),
        tool(QStringLiteral("schedule_task"),
             QStringLiteral("Put a task in its next free slot within its category's hours, around existing blocks and events."), id,
             {QStringLiteral("id")}),
    };
}

std::optional<QJsonObject> Server::handleLine(const QByteArray &line)
{
    if (line.trimmed().isEmpty())
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (!doc.isObject())
        return failure(QJsonValue::Null, -32700, QStringLiteral("Parse error: %1").arg(error.errorString()));
    return handle(doc.object());
}

std::optional<QJsonObject> Server::handle(const QJsonObject &message)
{
    const QJsonValue id = message.value(QLatin1String("id"));
    const bool isNotification = !message.contains(QLatin1String("id"));
    const QString method = message.value(QLatin1String("method")).toString();
    const QJsonObject params = message.value(QLatin1String("params")).toObject();

    if (method.startsWith(QLatin1String("notifications/")) || (isNotification && !method.isEmpty()))
        return std::nullopt;
    if (method.isEmpty()) // a response to us; we never send requests
        return std::nullopt;

    if (method == QLatin1String("initialize")) {
        const QString asked = params.value(QLatin1String("protocolVersion")).toString();
        const QString version = supportedVersions().contains(asked) ? asked : supportedVersions().first();
        return result(id, {
            {QStringLiteral("protocolVersion"), version},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{{QStringLiteral("listChanged"), false}}}}},
            {QStringLiteral("serverInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("task-tumbler")},
                                                       {QStringLiteral("title"), QStringLiteral("Task Tumbler")},
                                                       {QStringLiteral("version"), QStringLiteral("0.3.0")}}},
            {QStringLiteral("instructions"),
             QStringLiteral("Task Tumbler is the user's personal task manager (they have ADHD/ASD). Read before writing; prefer "
                            "small, concrete tasks; don't add or change things they didn't ask for. Every change can be undone in "
                            "the app with Ctrl+Z.")},
        });
    }
    if (method == QLatin1String("ping"))
        return result(id, {});
    if (method == QLatin1String("tools/list"))
        return result(id, {{QStringLiteral("tools"), tools()}});
    if (method == QLatin1String("tools/call"))
        return result(id, callTool(params.value(QLatin1String("name")).toString(), params.value(QLatin1String("arguments")).toObject()));
    return failure(id, -32601, QStringLiteral("Method not found: %1").arg(method));
}

QJsonObject Server::callTool(const QString &name, const QJsonObject &args)
{
    const QString id = args.value(QLatin1String("id")).toString();
    struct Call {
        QString method;
        QVariantList args;
        bool needsId;
    };
    const QHash<QString, Call> calls{
        {QStringLiteral("get_today"), {QStringLiteral("Today"), {}, false}},
        {QStringLiteral("list_tasks"), {QStringLiteral("Tasks"), {args.value(QLatin1String("filter")).toString(QStringLiteral("open")),
                                                                  args.value(QLatin1String("search")).toString()}, false}},
        {QStringLiteral("get_task"), {QStringLiteral("Task"), {id}, true}},
        {QStringLiteral("get_current"), {QStringLiteral("Current"), {}, false}},
        {QStringLiteral("add_task"), {QStringLiteral("Capture"), {args.value(QLatin1String("text")).toString()}, false}},
        {QStringLiteral("complete_task"), {QStringLiteral("Complete"), {id}, true}},
        {QStringLiteral("start_work"), {QStringLiteral("StartWork"), {id}, true}},
        {QStringLiteral("stop_work"), {QStringLiteral("StopWork"), {}, false}},
        {QStringLiteral("schedule_task"), {QStringLiteral("ScheduleNextSlot"), {id}, true}},
    };
    const auto it = calls.constFind(name);
    if (it == calls.constEnd())
        return toolText(QStringLiteral("Unknown tool: %1").arg(name), true);
    if (it->needsId && id.isEmpty())
        return toolText(QStringLiteral("This tool needs a task id"), true);
    if (name == QLatin1String("add_task") && args.value(QLatin1String("text")).toString().trimmed().isEmpty())
        return toolText(QStringLiteral("add_task needs some text"), true);

    QString error;
    const QVariant value = m_backend->call(it->method, it->args, &error);
    if (!error.isEmpty())
        return toolText(error, true);

    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? toolText(QStringLiteral("Done."))
                              : toolText(QStringLiteral("That didn't work (check the id, or whether it's already done)."), true);
    }
    const QString text = value.toString();
    if (name == QLatin1String("add_task")) {
        return text.isEmpty() ? toolText(QStringLiteral("Nothing was added."), true)
                              : toolText(QStringLiteral("{\"id\":\"%1\"}").arg(text));
    }
    return toolText(text);
}

int runStdio(Server &server)
{
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto reply = server.handleLine(QByteArray::fromStdString(line));
        if (!reply)
            continue;
        const QByteArray out = QJsonDocument(*reply).toJson(QJsonDocument::Compact);
        std::fwrite(out.constData(), 1, size_t(out.size()), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    return 0;
}

} // namespace tumbler::mcp
