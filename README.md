# Task Tumbler Desktop

A local, single-user desktop rebuild of [Task Tumbler](../task-tumbler): energy-aware task management and scheduling for ADHD/ASD brains, built with Qt 6 and QML. Data lives in one SQLite file, there is no sign-in, and AI comes from an Anthropic API key, a local Ollama model, or nothing at all.

The architecture and roadmap are in `../task-tumbler/docs/desktop-model.md`.

## Status: Phase 3b (phone companion)

- **On your phone, over your tailnet:** Today with Start / Done / Later / +15, Ready to Work, the running timer, an energy check-in, and quick capture with a live preview. Captures made while the computer is asleep wait on the phone and are sent later, without duplicates.
- **Notifications with buttons on Android:** Done, +15 min, Later, Skip. They go to the phone when you're away from the computer (idle or screen locked), or always, or never, as you choose in Settings.
- **How it works:**
  - The app serves a small web app and API on `127.0.0.1:7789`.
  - `tailscale serve` puts it on your tailnet with HTTPS.
  - You pair by scanning a QR code, which carries a token in the URL fragment, so it's never sent to a server.
  - Pushes use standard Web Push, encrypted per RFC 8291 and signed with VAPID.
  - There's no cloud service of ours; the only third party is your phone browser's push service.
- **Setup:**
  1. Settings → Phone → turn it on.
  2. Press **Detect**.
  3. Run the `tailscale serve …` command it shows, once.
  4. Scan the QR code with your phone.
  5. Add the page to your home screen, and turn on notifications from its ⋯ menu.

Phase 3:

- **Launchers:** "When I start this, open…" a website, a file, an app, or a command. They open when you press Start, or on demand.
  - Commands run as plain argv, with no shell.
  - A command asks once before it first runs, and again if it changes in any way.
- **Focus mode:** one task fills the window, with a big timer, its steps, its launchers, and full screen if you want it. The sidebar hides, and the desktop's Do Not Disturb is on while you're in it. The five-minute wrap-up warning still gets through.
- **Time away:** after 10 idle minutes mid-session (configurable), coming back asks whether to keep that time, leave the break out, or stop as of when you left. It never stops the timer on its own.
- **D-Bus API:** `org.kde.tasktumbler` at `/Tasks`. It has Today, Tasks, Task, Current, Capture, Complete, StartWork, StopWork, ScheduleNextSlot and Show, with JSON results.
- **CLI:** `task-tumbler --add "call mum tomorrow at 6pm"` captures into the running instance.
- **KRunner** (enable it in Settings): `tt <anything>` captures. Typing part of a task's name lets you open, start or finish it.
- **MCP server:** `task-tumbler --mcp` speaks the Model Context Protocol on stdio and drives the running app over D-Bus, so agents' changes are live and undoable.
  - Tools: get_today, list_tasks, get_task, get_current, add_task, complete_task, start_work, stop_work, schedule_task.
  - For Claude Code: `claude mcp add task-tumbler -- task-tumbler --mcp`.
  - Settings → "Let other apps change tasks" makes D-Bus, KRunner and MCP read-only.

Phase 2:

- **Plan my day** (Ctrl+P): fills free time with what matters, inside each category's hours and around blocks, events and routines. Demanding work goes in your best hours, easy work outside them, and prerequisites come first. Nothing is scheduled until you review the list, and accepting it is one undoable step. It works fully offline.
- **AI, optional:** Claude through an Anthropic API key (kept in KWallet), or a local model through Ollama.
  - The AI ranks and filters tasks and explains why. The offline planner still computes every time, so a small local model can't produce an impossible schedule.
  - Every answer is checked against a schema, with one retry. If the AI fails, you get the offline plan and the reason.
  - Also available: **Suggest steps** on a task, and **Ask AI** in quick capture (Ctrl+Enter) for messy notes.
  - Task notes go to cloud AI only if you turn that on. Every call is logged locally in `ai_logs`.
- **Energy:** one-tap check-ins (Wiped / Low / OK / Good / Great) on Today and in the planner. Low energy keeps peak-energy work off today's plan. The Energy page has sleep and readiness scores and a two-week chart.
- **Events:** create and edit them, including what may be scheduled during them (nothing, routines only, or certain tags).

Phase 1:

**App** (`task-tumbler`, Kirigami):
- **Today:** the running work timer, what's on right now, what didn't happen yet (with one-click "next free slot" / "tomorrow"), what's coming up, all-day events, and a Ready to Work grid. Ready to Work leaves out tasks that are waiting on unfinished prerequisites.
- **Tasks:** search, filters, and an at-a-glance status line for each task (blocked, due, next block, steps, routine). The detail page edits everything in place: steps, prerequisites, recurrence, and each block on the schedule.
- **Schedule:** a day timeline with category-hour shading, side-by-side lanes for overlaps, a "now" line, and blocks you can drag to move.
- **Quick capture:** one line of text with a live preview of what was understood ("call dentist tomorrow at 3pm ~15m #phone !high"). A time of day creates a block.
- **Undo:** every change, including deletes and notification actions, can be undone (Ctrl+Z).
- **Desktop integration:** a tray icon with the current task and elapsed time; notifications with Start / Bump / Done / Skip buttons; a global capture shortcut (Meta+Shift+Space); start at login; single instance; a daily backup that keeps the last 14.
- **First run:** import from the web app, or start fresh.

**Core** (`tumbler-core`, no GUI):
- Domain rules: recurrence, dependencies, event blocking, category windows.
- A deterministic free-slot finder.
- The Today planner, the reminder timing rules, and the quick-capture parser.
- Snapshot-based undo for any change.
- SQLite storage and migrations, and the web-app importer.

**Tests:** 16 suites. They cover the core, plus the app controller driven the way the QML drives it.

## Build and run

Requires Qt ≥ 6.8 (with Network and DBus), KDE Frameworks ≥ 6.8 (Kirigami, KirigamiAddons, I18n, CoreAddons, DBusAddons, Notifications, StatusNotifierItem, GlobalAccel, KIO, Service, IdleTime), QtKeychain, OpenSSL 3, KDE Prison (QR codes, runtime), CMake ≥ 3.20 and a C++20 compiler. `-DBUILD_APP=OFF` builds only the core, the CLI and the core tests.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/app/task-tumbler                 # uses ~/.local/share/task-tumbler/tumbler.db
./build/src/app/task-tumbler --db /tmp/t.db  # a separate test database, runs alongside
```

`task-tumbler --capture` opens quick capture (in the running instance if there is one). `--hidden` starts in the tray.

For UI work without opening windows (screenshot mode is sandboxed: no tray, shortcut, notifications, keyring or bus names): `QT_QPA_PLATFORM=offscreen TUMBLER_SCREENSHOT=out.png TUMBLER_PAGE=schedule ./build/src/app/task-tumbler --db test.db`. This renders a page, saves it and quits. Pages are `today`, `tasks`, `schedule`, `categories`, `settings`, `energy`, `onboarding`, `capture`, `plan`, `event:new`, `event:<id>` and `task:<id>`. `today|plan` runs steps 700 ms apart, like clicking through. `TUMBLER_PROBE=x,y` prints the visible items under a point.

## Moving data over from the web app

1. Export from the web app's database. Run this from `task-tumbler/server`; it needs Node 22.5 or newer for the SQLite path:

   ```bash
   # PostgreSQL (DATABASE_URL, or the docker default on :5433)
   npm run export-desktop -- --out ~/tumbler-export.json
   # or a legacy SQLite file
   npm run export-desktop -- --sqlite ../database.sqlite --user you@example.com --out ~/tumbler-export.json
   ```

   The export never includes passwords or OAuth tokens. Integrations come across as config only and need reconnecting.

2. Preview the import, then run it:

   ```bash
   ./build/src/cli/tumbler-import --dry-run ~/tumbler-export.json
   ./build/src/cli/tumbler-import ~/tumbler-export.json   # writes ~/.local/share/task-tumbler/tumbler.db
   ```

   Or use **Import from the web app** on the app's first-run screen, or in Settings.

   The import runs in a single transaction and only targets a new database. Every adjustment it makes is printed as a warning: merged duplicates, detached orphans, re-anchored recurrences, events entered in another timezone.

## Behaviour that intentionally differs from the web app

| Area | Web app | Desktop |
|---|---|---|
| Daily recurrence when the range starts before `startDate` | Produced occurrences before the start | Nothing before `startDate` |
| Weekly recurrence created mid-week | Dropped the rest of the first week | Days from the start date onward count |
| Every-N-weeks created mid-week | Fell on the wrong weeks | Weeks counted Sunday to Saturday from the start week. The importer re-anchors existing tasks so they keep the rhythm they already had |
| Recurrence without `startDate` | Anchored on whatever range was requested, so it drifted | `startDate` is always set. The importer uses the first occurrence or the creation date |
| Cancelled prerequisite | Still blocked the dependent task | No longer blocks |
| Event `blocking_mode` | Stored but never enforced | Enforced by the scheduler (`EventBlockingRule`) |
| Weekday of a date | `new Date("YYYY-MM-DD").getDay()`, which gives the previous day west of UTC | Calendar weekday of the local date (`weekday0`) |
| Ready to Work | Every open one-off task without a block today | Also leaves out tasks waiting on unfinished prerequisites, and parents whose steps are still open (the steps show instead) |
| Bumping a block that already passed | Shifted from its old start time, so it could land in the past | Shifted from now |
| Bumping past the due date | Refused | Allowed, with a note that it's now after the due date |
