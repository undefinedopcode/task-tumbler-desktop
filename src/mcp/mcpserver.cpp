#include "mcpserver.h"

#include <QJsonDocument>
#include <algorithm>
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
    const QJsonObject noteId{{QStringLiteral("id"), prop("string", QStringLiteral("Note id, from search_notes"))}};
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
        tool(QStringLiteral("search_notes"),
             QStringLiteral("The user's notes, newest first, or best match first when searching. Each has id, title, a snippet, tags "
                            "and the task it's about. Search matches every word as a prefix in titles and text."),
             {{QStringLiteral("query"), prop("string", QStringLiteral("Words to find; leave out to list recent notes"))},
              {QStringLiteral("tag"), prop("string", QStringLiteral("Only notes with this tag (no '#')"))}},
             {}, true),
        tool(QStringLiteral("get_note"), QStringLiteral("One note's full markdown text, tags and linked task."), noteId, {QStringLiteral("id")},
             true),
        tool(QStringLiteral("add_note"),
             QStringLiteral("Write a new markdown note, optionally about a task. Returns the new note's id."),
             {{QStringLiteral("title"), prop("string", QStringLiteral("Short title"))},
              {QStringLiteral("content"), prop("string", QStringLiteral("Markdown text"))},
              {QStringLiteral("task_id"), prop("string", QStringLiteral("Optional: the task it's about"))},
              {QStringLiteral("tags"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                                   {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                                   {QStringLiteral("description"), QStringLiteral("Optional tags, without '#'")}}}},
             {QStringLiteral("title"), QStringLiteral("content")}),
        tool(QStringLiteral("append_to_note"),
             QStringLiteral("Add a paragraph to the end of an existing note, leaving what's there untouched."),
             {{QStringLiteral("id"), prop("string", QStringLiteral("Note id, from search_notes or add_note"))},
              {QStringLiteral("text"), prop("string", QStringLiteral("Markdown to add"))}},
             {QStringLiteral("id"), QStringLiteral("text")}),
        tool(QStringLiteral("edit_note"),
             QStringLiteral("Change part of an existing note: replace old_text, copied exactly from get_note and appearing just "
                            "once, with new_text. Leave new_text empty to remove the passage. Change only what the user asked; "
                            "to add at the end, use append_to_note."),
             {{QStringLiteral("id"), prop("string", QStringLiteral("Note id, from search_notes or add_note"))},
              {QStringLiteral("old_text"), prop("string", QStringLiteral("The exact passage to replace, with enough around it to be unique"))},
              {QStringLiteral("new_text"), prop("string", QStringLiteral("What it becomes (markdown)"))}},
             {QStringLiteral("id"), QStringLiteral("old_text"), QStringLiteral("new_text")}),
        tool(QStringLiteral("get_stats"),
             QStringLiteral("Looking back: time worked (per day or week, per task, per hour of day with average energy), tasks and "
                            "routines finished, open tasks that keep getting moved, and how long finished tasks took against their "
                            "estimates (median ratio overall, by size and by category; 1.0 is spot on). Minutes throughout."),
             {{QStringLiteral("days"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                   {QStringLiteral("minimum"), 0},
                                                   {QStringLiteral("description"), QStringLiteral("How many days back, including today; "
                                                                                                  "0 for everything. Default 7")}}}},
             {}, true),
        tool(QStringLiteral("get_close_out"),
             QStringLiteral("Closing out today: what got finished and worked on, the leftovers (blocks that didn't get done, from "
                            "today or the last two weeks, and today's routines still open), today's day note, and tomorrow at a glance. "
                            "'closed' says whether the user has closed the day already."),
             {}, {}, true),
        tool(QStringLiteral("resolve_leftover"),
             QStringLiteral("Decide where one leftover from get_close_out goes. Blocks: 'tomorrow' (first free slot tomorrow), "
                            "'pile' (unschedule it; the task stays on the list), 'drop' (cancel the task), 'done'. Routines: 'done' "
                            "or 'skip'. Ask the user before dropping anything."),
             {{QStringLiteral("kind"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                   {QStringLiteral("enum"), QJsonArray{QStringLiteral("block"), QStringLiteral("routine")}},
                                                   {QStringLiteral("description"), QStringLiteral("The leftover's kind")}}},
              {QStringLiteral("id"), prop("string", QStringLiteral("The leftover's id (not its task id), from get_close_out"))},
              {QStringLiteral("choice"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                     {QStringLiteral("enum"), QJsonArray{QStringLiteral("tomorrow"), QStringLiteral("pile"),
                                                                                         QStringLiteral("drop"), QStringLiteral("done"),
                                                                                         QStringLiteral("skip")}},
                                                     {QStringLiteral("description"), QStringLiteral("Where it goes")}}}},
             {QStringLiteral("kind"), QStringLiteral("id"), QStringLiteral("choice")}),
        tool(QStringLiteral("clear_leftovers"),
             QStringLiteral("Unschedule all of today's leftovers and skip today's open routines, in one undoable step. The tasks "
                            "stay on the list for another day.")),
        tool(QStringLiteral("add_to_day_note"),
             QStringLiteral("Add a paragraph to today's day note (a note tagged 'day' that the weekly review reads back), starting it "
                            "if there isn't one. Never replaces what's there. Only write what the user asked to have noted."),
             {{QStringLiteral("text"), prop("string", QStringLiteral("Markdown to add"))}}, {QStringLiteral("text")}),
        tool(QStringLiteral("close_day"),
             QStringLiteral("Mark today closed out, so the app stops nudging about it.")),
        tool(QStringLiteral("let_go"),
             QStringLiteral("Let go of a task: cancel it and take its blocks off the schedule. For things that aren't going to "
                            "happen; ask the user first."),
             id, {QStringLiteral("id")}),
        tool(QStringLiteral("get_week_review"),
             QStringLiteral("The last seven days for a weekly review: time worked per day, what got finished and worked on, open "
                            "tasks that keep getting moved, tasks untouched for three weeks or more, tasks due within a week that "
                            "aren't scheduled (including overdue ones), and the week's day notes."),
             {}, {}, true),
        tool(QStringLiteral("finish_week_review"),
             QStringLiteral("Mark this week reviewed, so the app stops nudging about it.")),
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
                            "the app with Ctrl+Z. Notes are theirs too: search them for context, and only write one when asked. "
                            "Closing out the day or reviewing the week is about noticing what got done, not judging what didn't.")},
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
    QStringList tagList;
    for (const QJsonValue &t : args.value(QLatin1String("tags")).toArray())
        tagList.append(t.toString().trimmed().remove(QLatin1Char(',')));
    const QString tags = tagList.join(QLatin1Char(','));
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
        {QStringLiteral("search_notes"), {QStringLiteral("Notes"), {args.value(QLatin1String("query")).toString(),
                                                                   args.value(QLatin1String("tag")).toString()}, false}},
        {QStringLiteral("get_note"), {QStringLiteral("Note"), {id}, true}},
        {QStringLiteral("add_note"), {QStringLiteral("AddNote"), {args.value(QLatin1String("title")).toString(),
                                                                 args.value(QLatin1String("content")).toString(),
                                                                 args.value(QLatin1String("task_id")).toString(), tags}, false}},
        {QStringLiteral("append_to_note"), {QStringLiteral("AppendToNote"), {id, args.value(QLatin1String("text")).toString()}, true}},
        {QStringLiteral("edit_note"), {QStringLiteral("EditNote"), {id, args.value(QLatin1String("old_text")).toString(),
                                                                   args.value(QLatin1String("new_text")).toString()}, true}},
        {QStringLiteral("get_stats"), {QStringLiteral("Stats"), {std::max(0, args.value(QLatin1String("days")).toInt(7))}, false}},
        {QStringLiteral("get_close_out"), {QStringLiteral("CloseOut"), {}, false}},
        {QStringLiteral("resolve_leftover"), {QStringLiteral("ResolveLeftover"), {args.value(QLatin1String("kind")).toString(), id,
                                                                                args.value(QLatin1String("choice")).toString()}, true}},
        {QStringLiteral("clear_leftovers"), {QStringLiteral("ClearLeftovers"), {}, false}},
        {QStringLiteral("add_to_day_note"), {QStringLiteral("AddToDayNote"), {args.value(QLatin1String("text")).toString()}, false}},
        {QStringLiteral("close_day"), {QStringLiteral("FinishCloseOut"), {}, false}},
        {QStringLiteral("let_go"), {QStringLiteral("LetGo"), {id}, true}},
        {QStringLiteral("get_week_review"), {QStringLiteral("WeekReview"), {}, false}},
        {QStringLiteral("finish_week_review"), {QStringLiteral("FinishReview"), {}, false}},
    };
    const auto it = calls.constFind(name);
    if (it == calls.constEnd())
        return toolText(QStringLiteral("Unknown tool: %1").arg(name), true);
    const bool aboutNotes = name.contains(QLatin1String("note"));
    if (it->needsId && id.isEmpty()) {
        return toolText(aboutNotes ? QStringLiteral("This tool needs a note id")
                        : name == QLatin1String("resolve_leftover") ? QStringLiteral("This tool needs a leftover id, from get_close_out")
                                                                    : QStringLiteral("This tool needs a task id"),
                        true);
    }
    if (name == QLatin1String("resolve_leftover")) {
        const QString kind = args.value(QLatin1String("kind")).toString();
        const QString choice = args.value(QLatin1String("choice")).toString();
        const QStringList choices = kind == QLatin1String("routine") ? QStringList{QStringLiteral("done"), QStringLiteral("skip")}
            : kind == QLatin1String("block") ? QStringList{QStringLiteral("tomorrow"), QStringLiteral("pile"), QStringLiteral("drop"), QStringLiteral("done")}
                                             : QStringList{};
        if (choices.isEmpty())
            return toolText(QStringLiteral("kind must be 'block' or 'routine'"), true);
        if (!choices.contains(choice))
            return toolText(QStringLiteral("For a %1, choice must be one of: %2").arg(kind, choices.join(QStringLiteral(", "))), true);
    }
    if ((name == QLatin1String("add_task") || name == QLatin1String("append_to_note") || name == QLatin1String("add_to_day_note"))
        && args.value(QLatin1String("text")).toString().trimmed().isEmpty())
        return toolText(QStringLiteral("%1 needs some text").arg(name), true);
    if (name == QLatin1String("edit_note") && args.value(QLatin1String("old_text")).toString().isEmpty())
        return toolText(QStringLiteral("edit_note needs the old_text to replace; to add at the end, use append_to_note"), true);
    if (name == QLatin1String("add_note") && args.value(QLatin1String("title")).toString().trimmed().isEmpty()
        && args.value(QLatin1String("content")).toString().trimmed().isEmpty())
        return toolText(QStringLiteral("add_note needs a title or some content"), true);

    QString error;
    const QVariant value = m_backend->call(it->method, it->args, &error);
    if (!error.isEmpty())
        return toolText(error, true);

    if (value.typeId() == QMetaType::Bool) {
        return value.toBool() ? toolText(QStringLiteral("Done."))
                              : toolText(QStringLiteral("That didn't work (check the id, or whether it's already done)."), true);
    }
    const QString text = value.toString();
    if (name == QLatin1String("add_task") || name == QLatin1String("add_note") || name == QLatin1String("add_to_day_note")) {
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
