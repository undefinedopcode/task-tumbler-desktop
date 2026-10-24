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

## Layout
- `src/core/`: `tumbler-core`, the domain rules and storage. QtCore and QtSql only, never GUI.
- `src/app/`: the Kirigami app. `AppController` is the only object QML talks to. Every write goes through `AppController::apply(Mutation)`, which puts it on the undo stack. `desktop/` holds the tray, reminders and autostart.
- `src/app/qml/`: `Main.qml`, `pages/`, `components/`. `Format.qml` is a singleton; singletons can't see `i18n()`, so it uses `qsTr`.
- `src/ai/`: `tumbler-ai`, the providers (Claude, Ollama) and each feature's prompt, schema and answer handling (`aifeatures.*`). The AI only ranks and filters; `core/dayplanner` computes every time. Treat answers as untrusted: drop unknown ids, clamp values.
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
- Never name a header after a system header (`features.h`, `types.h`, `time.h`...) in a directory on the include path: it silently replaces glibc's.
- Short side trips (Plan my day) open with `pageStack.layers.push`, not as a second column. Columns slide the previous page under the drawer, where Kirigami cards draw over it.
- Logic with rules worth testing (timing, ordering, parsing) belongs in core as a pure function, with the app layer as a thin caller. See `reminderplan.h` and `today.h`.
