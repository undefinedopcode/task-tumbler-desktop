# Task Tumbler Desktop - Project Instructions

Qt 6 / QML desktop rebuild of the Task Tumbler web app (`../task-tumbler`, kept as the reference implementation). Architecture and roadmap: `../task-tumbler/docs/desktop-model.md`.

## Commands
```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
./build/src/app/task-tumbler --db /tmp/t.db          # never test against the real ~/.local/share db
QT_QPA_PLATFORM=offscreen TUMBLER_SCREENSHOT=out.png TUMBLER_PAGE=tasks ./build/src/app/task-tumbler --db /tmp/t.db
cmake --build build --target task-tumbler_qmllint    # "unqualified" warnings (i18n, applicationWindow) are expected
QT_FORCE_STDERR_LOGGING=1 ...                         # otherwise Qt/QML warnings go to the journal, not the terminal
```
AI tests use `tests/fakeserver.h` (a local HTTP server); never call the real APIs from tests.

**Never let test runs touch the user's desktop session.** Only run the app in sandboxed screenshot mode (`TUMBLER_SCREENSHOT`, which skips the tray, global shortcut, notifications, keyring and bus names) with `QT_QPA_PLATFORM=offscreen` and a scratch `--db`. `dbus-run-session` doesn't isolate it: portals and the keyring daemon start in the real session, and a KWallet prompt appeared on the user's screen once. Stop test processes by exact PID, never with broad `pkill -f` patterns.

## Layout
- `src/core/`: `tumbler-core`, the domain rules and storage. QtCore and QtSql only, never GUI.
- `src/app/`: the Kirigami app. `AppController` is the only object QML talks to. Every write goes through `AppController::apply(Mutation)`, which puts it on the undo stack. `desktop/` holds the tray, reminders and autostart.
- `src/app/qml/`: `Main.qml`, `pages/`, `components/`. `Format.qml` is a singleton; singletons can't see `i18n()`, so it uses `qsTr`.
- `src/ai/`: `tumbler-ai`, the providers (Claude, Ollama) and each feature's prompt, schema and answer handling (`aifeatures.*`). The AI only ranks and filters; `core/dayplanner` computes every time. Treat answers as untrusted: drop unknown ids, clamp values.
- `src/mcp/`: the MCP server (`task-tumbler --mcp`). Protocol handling in `mcpserver.*` against a `Backend` interface; `dbusbackend.*` calls the running app. Tested with a fake backend.
- `src/app/desktop/`: OS integration that only executes: launcher (KIO), idle watch (KIdleTime), focus guard (DND), D-Bus API, KRunner adaptor. Policy (what to launch, approvals, away/trim, runner matching, write switch) lives in `appcontroller_desktop.cpp` / `appcontroller_external.cpp` so it can be tested.
- `src/remote/`: `tumbler-remote`, the phone companion's server side.
  - `httpserver.*`: the HTTP API and event stream, with auth, lockout, origin checks and bind policy, against a `Backend` interface.
  - `webpush.*`: RFC 8291 encryption and VAPID signing, with OpenSSL.
  - `web/`: the phone app (vanilla JS PWA, embedded as `:/remote/web`).
- `src/app/remote/remotehub.*`: implements that backend over AppController. It's the second QML singleton, `RemoteHub`, which drives the Settings → Phone section. Secrets live in `~/.config/task-tumbler/remote.json` (0600).
- `src/core/sync.*`: the sync rules as plain database logic: pull tasks/events, close what vanished, tombstones for tasks deleted here, completions to push. Migration 002 makes `synced_items.task_id` ON DELETE SET NULL for the tombstones.
- `src/sync/`: `tumbler-sync`, the service clients: `linear.*` (GraphQL, personal key, no "Bearer") and `icsfeed.*` (KCalendarCore expansion, fetching). They fetch and translate only.
- `src/app/sync/synchub.*`: the third QML singleton, `SyncHub`. It runs the timers, reads the keyring (only once Linear is connected), and drives Settings → Linear and Calendars. Sync writes go through `AppController::applyExternal`, outside the undo stack.
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
- QML combo boxes: set `currentIndex` from a computed index, not `indexOfValue()`, which returns -1 in bindings until the model loads.
- Tests that run a server pick a free port at runtime, never a fixed one.
- Sync is tested against `tests/fakeserver.h` and local `.ics` files, never the real Linear API or real calendars. In screenshot mode SyncHub gets a `NullKeyStore` and never syncs on its own.
- `tumbler::Event` and `KCalendarCore::Event` clash: always qualify the KCalendarCore one.
- Phone UI checks: headless Firefox with a throwaway profile (`firefox --headless -no-remote -profile <scratch> --screenshot …`) against a sandboxed instance (`TUMBLER_SHOT_DELAY=30000`, `remote_enabled` set in a scratch DB).
- The phone app's CSP forbids inline styles: set colours through `element.style`, never `style=""`.
- The keyring is only opened when Claude is the selected provider (opening a closed KWallet prompts).
- moc can't parse raw string literals containing quotes in a Q_OBJECT file; use ordinary escaped strings in tests.
- Never name a header after a system header (`features.h`, `types.h`, `time.h`...) in a directory on the include path: it silently replaces glibc's.
- Short side trips (Plan my day) open with `pageStack.layers.push`, not as a second column. Columns slide the previous page under the drawer, where Kirigami cards draw over it.
- Logic with rules worth testing (timing, ordering, parsing) belongs in core as a pure function, with the app layer as a thin caller. See `reminderplan.h` and `today.h`.
