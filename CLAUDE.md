# Task Tumbler Desktop - Project Instructions

Qt 6 desktop rebuild of the Task Tumbler web app, with a React UI in QtWebEngine (`../task-tumbler`, kept as the reference implementation). Architecture and roadmap: `../task-tumbler/docs/desktop-model.md`.

## Commands
```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
./build/src/app/task-tumbler --db /tmp/t.db          # never test against the real ~/.local/share db
QT_QPA_PLATFORM=offscreen TUMBLER_SCREENSHOT=out.png TUMBLER_PAGE=tasks ./build/src/app/task-tumbler --db /tmp/t.db
QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES="js=true" ...   # Qt warnings and the page's console.log on the terminal
```
The UI (`webui/`, React):
```bash
cd webui && npm run dev                                  # browser at :5173 with mock data (bridge/mock.ts)
./build/src/app/task-tumbler --dev-url http://localhost:5173 --db /tmp/t.db   # hot reload against the real app
QT_QPA_PLATFORM=offscreen TUMBLER_SCREENSHOT=out.png TUMBLER_PAGE="today|js:<code>|capture|capture-js:<code>" TUMBLER_COLOR_SCHEME=dark ...
cd webui && npx tsc -b                                   # typecheck (the CMake build runs it too)
```
Scripted steps: set input values with the native setter plus an `input` event, wrap each step in `(()=>{…})()` (steps share the page's global scope), and use pointer events for things that open on pointer-up (schedule blocks).

AI tests use `tests/fakeserver.h` (a local HTTP server); never call the real APIs from tests.

**Never let test runs touch the user's desktop session.** Every launch of the real binary in a test gets `TUMBLER_SCREENSHOT`, including `--add` handoff tests and their retries (start the listener first). A `--db` instance never touches the login item, KRunner plugin or D-Bus name, but still registers the global shortcut and shows a tray icon and notifications when not sandboxed. Only run the app in sandboxed screenshot mode (`TUMBLER_SCREENSHOT`, which skips the tray, global shortcut, notifications, keyring and bus names) with `QT_QPA_PLATFORM=offscreen` and a scratch `--db`. `dbus-run-session` doesn't isolate it: portals and the keyring daemon start in the real session, and a KWallet prompt appeared on the user's screen once. Stop test processes by exact PID, never with broad `pkill -f` patterns.

## Layout
- `src/core/`: `tumbler-core`, the domain rules and storage. QtCore and QtSql only, never GUI.
- `src/app/`: the app. `AppController` is the only object the UI talks to. Every write goes through `AppController::apply(Mutation)`, which puts it on the undo stack. `desktop/` holds the tray, reminders and autostart.
- `webui/`: the React UI (Vite, TypeScript, Tailwind v4, Radix primitives, lucide icons). CMake runs `npm ci`/`npm run build` into one inlined `index.html`, embedded as `:/webui` and served as `tumbler://app/` (`src/app/web/appscheme.*`). `src/app/web/webshell.*` is the window (QWebEngineView) and publishes `app` (AppController), `sync`, `remote` and `shell` (`shellbridge.*`: platform, colour scheme, file dialogs) over a QWebChannel. In React, `bridge/hooks.ts` gives `useProp` (a property kept current by its NOTIFY signal; list unusual signal names in `notifyOf`), `call`, `useSignal` and `useQuery`. Models don't cross the channel: expose rows as maps (`TaskListModel::rows()` + `rowsChanged`). Design tokens live in `src/index.css`; one look on every platform, light/dark from the system.
- `src/ai/`: `tumbler-ai`, the providers (Claude API, Claude Code CLI, Ollama) and each feature's prompt, schema and answer handling (`aifeatures.*`). The AI only ranks and filters; `core/dayplanner` computes every time. Treat answers as untrusted: drop unknown ids, clamp values. `runValidated` repairs shape slips (`coerce`) before validating; `tests/tst_ai_live` tries real models on request.
- `src/mcp/`: the MCP server (`task-tumbler --mcp`). Protocol handling in `mcpserver.*` against a `Backend` interface; `dbusbackend.*` calls the running app. Tested with a fake backend.
- `src/app/desktop/`: OS integration that only executes: launcher (KIO), idle watch (KIdleTime), focus guard (DND), D-Bus API, KRunner adaptor. Policy (what to launch, approvals, away/trim, runner matching, write switch) lives in `appcontroller_desktop.cpp` / `appcontroller_external.cpp` so it can be tested.
- `src/remote/`: `tumbler-remote`, the phone companion's server side.
  - `httpserver.*`: the HTTP API and event stream, with auth, lockout, origin checks and bind policy, against a `Backend` interface.
  - `webpush.*`: RFC 8291 encryption and VAPID signing, with OpenSSL.
  - `web/`: the phone app (vanilla JS PWA, embedded as `:/remote/web`).
- `src/app/remote/remotehub.*`: implements that backend over AppController. It's published to the UI as `remote` and drives the Settings → Phone section. Secrets live in `~/.config/task-tumbler/remote.json` (0600).
- `src/core/sync.*`: the sync rules as plain database logic: pull tasks/events, close what vanished, tombstones for tasks deleted here, completions to push. Migration 002 makes `synced_items.task_id` ON DELETE SET NULL for the tombstones.
- `src/sync/`: `tumbler-sync`, the service clients: `linear.*` (GraphQL, personal key, no "Bearer") and `icsfeed.*` (KCalendarCore expansion, fetching). They fetch and translate only.
- `src/app/sync/synchub.*`: published to the UI as `sync`. It runs the timers, reads the keyring (only once Linear is connected), and drives Settings → Linear and Calendars. Sync writes go through `AppController::applyExternal`, outside the undo stack.
- `src/core/notes.*`: notes over the FTS5 table from 001 (`ftsQuery` turns typing into a safe prefix query; snippets mark matches with \x02/\x03 and the controller turns them into `<b>`). Note edits save through `applyExternal` (not undoable per keystroke); create and delete are undoable.
- `src/cli/`: command-line tools.
- `resources/migrations/NNN_name.sql`: schema migrations, embedded as `:/migrations` and applied by `Database::migrate()` using `PRAGMA user_version`. Never edit a migration that has shipped; add a new numbered file and list it in `src/core/CMakeLists.txt`.
- `tests/`: Qt Test, one `tst_*.cpp` per area, registered in `tests/CMakeLists.txt`. Fixtures live in `tests/fixtures/`.

## Writes and undo
- Build writes in `core/operations.cpp` as a `Mutation`: a list of `Scope`s plus an `apply` function. Scopes must cover every row the change touches, including cascades. Generate new ids before building, so the scopes can name them.
- `MutationRunner` snapshots the scoped rows before and after and undoes by moving between the two snapshots. It never deletes and re-inserts a surviving row, because that would fire cascades.
- Derived data (occurrence materialisation) and imports are deliberately not undoable.

## Conventions
- C++20, namespace `tumbler`, lowercase file names, Qt naming for classes and methods.
- Storage: instants are INTEGER UTC milliseconds. Wall-clock intents (due dates, recurring times, category windows, all-day events) are local `YYYY-MM-DD` / `HH:MM` text. JSON columns are TEXT with `json_valid` checks.
- Weekdays use the web app's convention, 0 = Sunday. Always go through `weekday0(QDate)`.
- Secrets (API keys, OAuth tokens) go in the OS keychain, never in SQLite.
- Every behaviour change from the web app gets a test and a row in the README's "intentionally differs" table.
- Tests that run a server pick a free port at runtime, never a fixed one.
- Sync is tested against `tests/fakeserver.h` and local `.ics` files, never the real Linear API or real calendars. In screenshot mode SyncHub gets a `NullKeyStore` and never syncs on its own.
- `tumbler::Event` and `KCalendarCore::Event` clash: always qualify the KCalendarCore one.
- Phone UI checks: headless Firefox with a throwaway profile (`firefox --headless -no-remote -profile <scratch> --screenshot …`) against a sandboxed instance (`TUMBLER_SHOT_DELAY=30000`, `remote_enabled` set in a scratch DB).
- The phone app's CSP forbids inline styles: set colours through `element.style`, never `style=""`.
- The keyring is only opened when Claude is the selected provider (opening a closed KWallet prompts).
- moc can't parse raw string literals containing quotes in a Q_OBJECT file; use ordinary escaped strings in tests.
- Never name a header after a system header (`features.h`, `types.h`, `time.h`...) in a directory on the include path: it silently replaces glibc's.
- In the UI, subscribe to a signal (`useSignal`) before making the request whose answer it carries (`planDay` → `planReady`); hooks connect at once when the channel is up.
- The web view only loads its own pages (`ShellPage`); links go to the browser through it or `shell.openExternal`, and only log in screenshot mode.
- Logic with rules worth testing (timing, ordering, parsing) belongs in core as a pure function, with the app layer as a thin caller. See `reminderplan.h` and `today.h`.
