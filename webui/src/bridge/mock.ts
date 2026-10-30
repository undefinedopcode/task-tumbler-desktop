// Sample data for working on the UI in a browser (npm run dev). Shaped like
// the real objects: properties, <name>Changed signals, callback methods.

/* eslint-disable @typescript-eslint/no-explicit-any */
import type { AgendaItem, ReadyTask, Today } from './types'
import type { Objects, Signal } from './channel'

function signal(): Signal & { emit: (...args: any[]) => void } {
  const fns = new Set<(...args: any[]) => void>()
  return {
    connect: (fn) => fns.add(fn),
    disconnect: (fn) => fns.delete(fn),
    emit: (...args) => fns.forEach((fn) => fn(...args)),
  }
}

function id() {
  return Math.random().toString(36).slice(2, 10)
}

const HOUR = 3600_000

function at(h: number, m = 0) {
  const d = new Date()
  d.setHours(h, m, 0, 0)
  return d.getTime()
}

function task(title: string, extra: Partial<ReadyTask> = {}): ReadyTask {
  return {
    id: id(),
    title,
    description: '',
    status: 'pending',
    priority: 'medium',
    energy: 'medium',
    estimate: 30,
    complexity: 2,
    dueDate: '',
    scheduledTime: '',
    fixedTime: false,
    parentId: '',
    categoryId: '',
    tags: [],
    color: '',
    isRecurring: false,
    createdAt: Date.now(),
    subtasksDone: 0,
    subtasksTotal: 0,
    ...extra,
  }
}

function block(title: string, start: number, minutes: number, extra: Partial<AgendaItem> = {}): AgendaItem {
  const now = Date.now()
  const end = start + minutes * 60_000
  return {
    kind: 'schedule',
    state: end < now ? 'overdue' : start <= now ? 'now' : 'upcoming',
    id: id(),
    taskId: id(),
    title,
    start,
    end,
    allDay: false,
    timeless: false,
    working: false,
    startsSoon: start > now && start - now < 15 * 60_000,
    bumpedCount: 0,
    color: '',
    priority: 'medium',
    energy: 'medium',
    ...extra,
  }
}

export function createMock(): Objects {
  const ready: ReadyTask[] = [
    task('Reply to the landlord about the boiler', { priority: 'high', estimate: 15, energy: 'low', tags: ['home'], dueDate: new Date().toISOString().slice(0, 10) }),
    task('Draft the Q4 planning doc', { priority: 'urgent', estimate: 90, energy: 'peak', subtasksDone: 2, subtasksTotal: 5, tags: ['work'] }),
    task('Book GP appointment', { estimate: 10, energy: 'low', tags: ['health'] }),
    task('Sort the photo backup', { priority: 'low', estimate: 45, energy: 'medium' }),
  ]
  const now = Date.now()
  const today: Today = {
    date: new Date().toISOString().slice(0, 10),
    dateLabel: new Date().toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long' }),
    allDay: [block("Sam's birthday", at(0), 24 * 60, { kind: 'event', allDay: true, taskId: '', color: '#e5484d' })],
    now: [block('Deep work: API design', now - 0.5 * HOUR, 90, { color: '#5b5bd6', energy: 'peak' })],
    upcoming: [
      block('Standup', now + 0.2 * HOUR, 15, { kind: 'event', taskId: '', color: '#12a594' }),
      block('Walk', now + 2 * HOUR, 30, { kind: 'occurrence', energy: 'low', color: '#30a46c' }),
      block('Review pull requests', now + 3 * HOUR, 45),
    ],
    overdue: [block('Pay the electricity bill', now - 3 * HOUR, 15, { bumpedCount: 2, priority: 'high' })],
    done: [block('Morning meds', at(8), 5, { kind: 'occurrence', state: 'done', timeless: true })],
    ready,
    readyTotal: 9,
    closeOutDue: false,
    reviewDue: false,
  }

  const app: any = {
    today,
    working: { id: id(), taskId: today.now[0].taskId, startedAt: now - 23 * 60_000, title: 'Deep work: API design' },
    energy: { level: 6, at: now - 2 * HOUR, label: 'OK' },
    away: {},
    settings: { closeToTray: true },
    canUndo: false,
    canRedo: false,
    undoText: '',
    redoText: '',
    needsOnboarding: false,
    focusMode: false,
    categories: [],
    ai: { provider: 'off', ready: false },
  }
  for (const k of Object.keys(app)) app[`${k}Changed`] = signal()
  app.todayChanged = signal()
  app.undoStateChanged = signal()
  app.changed = signal()
  app.message = signal()
  app.showCaptureRequested = signal()
  app.pageRequested = signal()

  const update = (patch: Partial<Today>, text?: string) => {
    app.today = { ...app.today, ...patch }
    app.todayChanged.emit()
    app.changed.emit(['tasks'])
    app.canUndo = true
    app.undoText = text ?? 'change'
    app.undoStateChanged.emit()
  }
  const reply = (fn: any, value: unknown) => setTimeout(() => fn?.(value), 10)

  app.previewCapture = (text: string, cb: any) => {
    const tags = [...text.matchAll(/#(\w+)/g)].map((m) => m[1])
    const est = text.match(/~(\d+)m?\b/)
    reply(cb, { title: text.replace(/#\w+|~\d+m?\b/g, '').trim(), tags, estimate: est ? Number(est[1]) : undefined })
  }
  app.capture = (text: string, cb: any) => {
    const t = task(text.replace(/#\w+|~\d+m?\b/g, '').trim())
    update({ ready: [t, ...app.today.ready], readyTotal: app.today.readyTotal + 1 }, 'capture')
    app.message.emit(`Added “${t.title}”`)
    reply(cb, t.id)
  }
  app.startWork = (taskId: string, cb: any) => {
    const t = app.today.ready.find((r: ReadyTask) => r.id === taskId)
    app.working = { id: id(), taskId, startedAt: Date.now(), title: t?.title }
    app.workingChanged.emit()
    reply(cb, true)
  }
  app.stopWork = (cb: any) => {
    app.working = {}
    app.workingChanged.emit()
    reply(cb, true)
  }
  app.setTaskStatus = (taskId: string, status: string, cb: any) => {
    if (status === 'completed') {
      update({ ready: app.today.ready.filter((r: ReadyTask) => r.id !== taskId) }, 'finish task')
      if (app.working.taskId === taskId) app.stopWork()
    }
    reply(cb, true)
  }
  app.scheduleNextSlot = (taskId: string, cb: any) => {
    const t = app.today.ready.find((r: ReadyTask) => r.id === taskId)
    if (t) {
      update(
        {
          ready: app.today.ready.filter((r: ReadyTask) => r.id !== taskId),
          upcoming: [...app.today.upcoming, block(t.title, Date.now() + 4 * HOUR, t.estimate)],
        },
        'schedule',
      )
      app.message.emit(`Scheduled “${t.title}” for later today`)
    }
    reply(cb, true)
  }
  const resolveBlock = (blockId: string, done: boolean) => {
    const lists = ['now', 'upcoming', 'overdue'] as const
    const patch: Partial<Today> = {}
    let found: AgendaItem | undefined
    for (const l of lists) {
      const item = app.today[l].find((i: AgendaItem) => i.id === blockId)
      if (item) found = item
      patch[l] = app.today[l].filter((i: AgendaItem) => i.id !== blockId)
    }
    if (found && done) patch.done = [...app.today.done, { ...found, state: 'done' }]
    update(patch, done ? 'finish block' : 'move block')
  }
  app.completeSchedule = (blockId: string, cb: any) => (resolveBlock(blockId, true), reply(cb, true))
  app.setOccurrence = (blockId: string, status: string, cb: any) => (resolveBlock(blockId, status === 'completed'), reply(cb, true))
  app.bump = (blockId: string, _m: number, cb: any) => (resolveBlock(blockId, false), reply(cb, true))
  app.bumpToNextSlot = (blockId: string, cb: any) => (resolveBlock(blockId, false), reply(cb, true))
  app.bumpToTomorrow = (blockId: string, cb: any) => (resolveBlock(blockId, false), reply(cb, true))
  app.unschedule = (blockId: string, cb: any) => (resolveBlock(blockId, false), reply(cb, true))
  app.undo = () => {
    app.canUndo = false
    app.undoStateChanged.emit()
    app.message.emit('Undone')
  }
  app.redo = () => {}
  app.logEnergy = (level: number, _n: string, cb: any) => {
    app.energy = { level, at: Date.now(), label: ({ 2: 'Wiped', 4: 'Low', 6: 'OK', 8: 'Good', 10: 'Great' } as Record<number, string>)[level] }
    app.energyChanged.emit()
    reply(cb, true)
  }

  // The Tasks list and task details, from the same sample tasks.
  const extra: ReadyTask[] = [
    task('Water the plants', { isRecurring: true, recurrenceText: 'Every 3 days', estimate: 10, energy: 'low' }),
    task('File the expense report', { status: 'completed', estimate: 20 }),
  ]
  const all = () => [...app.today.ready, ...extra]
  const tasks: any = { filter: 'active', search: '', categoryId: '', totalActive: 0, count: 0 }
  for (const k of ['filter', 'search', 'categoryId', 'count']) tasks[`${k}Changed`] = signal()
  tasks.rowsChanged = signal()
  tasks.countChanged = signal()
  tasks.rows = (cb: any) => {
    const done = (t: ReadyTask) => t.status === 'completed' || t.status === 'cancelled'
    const rows = all()
      .filter((t) => (tasks.filter === 'active' ? !done(t) : tasks.filter === 'completed' ? done(t) : true))
      .filter((t) => !tasks.search || t.title.toLowerCase().includes(String(tasks.search).toLowerCase()))
      .map((t) => ({
        ...t,
        taskId: t.id,
        dueState: '',
        categoryName: 'Default',
        categoryColor: '#5b5bd6',
        recurrenceText: t.recurrenceText ?? '',
        blocked: false,
        parentTitle: '',
        nextBlock: 0,
        loggedMinutes: 0,
        taskColor: '',
      }))
    reply(cb, rows)
  }
  // Property writes from the page land here; say the rows moved.
  const tasksProxy = new Proxy(tasks, {
    set(target, prop, value) {
      target[prop] = value
      target[`${String(prop)}Changed`]?.emit()
      target.rowsChanged.emit()
      return true
    },
  })
  app.tasks = tasksProxy
  app.changed.connect(() => tasks.rowsChanged.emit())
  app.categories = [
    { id: 'c1', name: 'Default', color: '#5b5bd6', isDefault: true, showOnSchedule: true },
    { id: 'c2', name: 'Work', color: '#12a594', isDefault: false, showOnSchedule: true },
  ]
  const details = new Map<string, any>()
  app.taskDetail = (id: string, cb: any) => {
    const t = all().find((x) => x.id === id)
    if (!t) return reply(cb, {})
    const d = details.get(id) ?? { subtasks: [], prerequisites: [], dependents: [], schedules: [], actions: [], loggedMinutes: 25 }
    details.set(id, d)
    reply(cb, { ...t, ...d })
  }
  app.updateTask = (id: string, fields: any, cb: any) => {
    const t = all().find((x) => x.id === id)
    if (t) {
      const map: Record<string, string> = { energy_required: 'energy', time_estimate_minutes: 'estimate', due_date: 'dueDate', category_id: 'categoryId' }
      for (const [k, v] of Object.entries(fields)) (t as any)[map[k] ?? k] = v
      if ('recurrence' in fields) {
        t.isRecurring = !!fields.recurrence
        ;(details.get(id) ?? {}).recurrence = fields.recurrence ?? undefined
        details.set(id, { ...(details.get(id) ?? {}), recurrence: fields.recurrence ?? undefined })
        t.recurrenceText = fields.recurrence ? 'Every day' : undefined
      }
      app.todayChanged.emit()
      app.changed.emit(['tasks'])
    }
    reply(cb, true)
  }
  app.addSubtask = (parentId: string, title: string, cb: any) => {
    const d = details.get(parentId)
    d?.subtasks.push(task(title, { parentId }))
    app.changed.emit(['tasks'])
    reply(cb, id())
  }
  app.prerequisiteCandidates = (taskId: string, cb: any) => reply(cb, all().filter((t) => t.id !== taskId))
  const notes: any[] = [
    { id: 'n1', title: 'Boiler call', content: '## Boiler\n\nThe **landlord** said Thursday.\n\n- [ ] Find the warranty', tags: ['home'], color: '#f59e0b', taskId: '', taskTitle: '', updatedAt: Date.now() - HOUR },
  ]
  const summary = (n: any) => ({ ...n, snippet: n.content.slice(0, 120).replace(/[#*\n]+/g, ' ') })
  app.notes = (search: string, tag: string, taskId: string, cb: any) =>
    reply(
      cb,
      notes
        .filter((n) => (!search || `${n.title} ${n.content}`.toLowerCase().includes(search.toLowerCase())) && (!tag || n.tags.includes(tag)) && (!taskId || n.taskId === taskId))
        .map(summary),
    )
  app.noteTags = (cb: any) => reply(cb, [...new Set(notes.flatMap((n) => n.tags))])
  app.noteTaskChoices = (cb: any) => reply(cb, app.today.ready.map((r: ReadyTask) => ({ id: r.id, title: r.title })))
  app.noteDetail = (noteId: string, cb: any) => reply(cb, notes.find((n) => n.id === noteId) ?? {})
  app.createNote = (fields: any, cb: any) => {
    const n = { id: id(), title: fields.title ?? 'Untitled note', content: '', tags: fields.tags ?? [], color: '', taskId: fields.taskId ?? '', taskTitle: '', updatedAt: Date.now() }
    notes.unshift(n)
    app.changed.emit(['notes'])
    reply(cb, n.id)
  }
  app.saveNote = (noteId: string, fields: any, cb: any) => {
    const n = notes.find((x) => x.id === noteId)
    if (n) Object.assign(n, fields, { updatedAt: Date.now() })
    app.changed.emit(['notes'])
    reply(cb, true)
  }
  app.deleteNote = (noteId: string, cb: any) => {
    notes.splice(notes.findIndex((x) => x.id === noteId), 1)
    app.changed.emit(['notes'])
    reply(cb, true)
  }
  app.installedApps = (cb: any) => reply(cb, [{ id: 'firefox', name: 'Firefox', icon: 'firefox' }])
  app.addAction = (_t: string, _k: string, _x: string, _o: boolean, cb: any) => reply(cb, id())
  app.deleteTask = (taskId: string, cb: any) => {
    update({ ready: app.today.ready.filter((r: ReadyTask) => r.id !== taskId) }, 'delete task')
    reply(cb, true)
  }
  app.openTaskRequested = signal()
  app.stepsReady = signal()
  app.aiFailed = signal()
  app.commandNeedsApproval = signal()
  app.timeCheckNeeded = signal()

  app.dayAgenda = (iso: string, cb: any) => {
    const start = new Date(`${iso}T00:00:00`).getTime()
    const isToday = iso === app.today.date
    const t: Today = app.today
    reply(cb, {
      date: iso,
      label: new Date(start).toLocaleDateString(undefined, { weekday: 'long', day: 'numeric', month: 'long' }),
      dayStart: start,
      dayEnd: start + 24 * HOUR,
      isToday,
      items: isToday ? [...t.now, ...t.upcoming, ...t.overdue, ...t.done] : [],
      allDay: isToday ? t.allDay : [],
      windows: [{ name: 'Default', color: '#5b5bd6', start: start + 6 * HOUR, end: start + 23 * HOUR }],
    })
  }
  app.planReady = signal()
  app.planDay = (iso: string) =>
    setTimeout(
      () =>
        app.planReady.emit({
          date: iso,
          label: iso,
          blocks: app.today.ready.slice(0, 3).map((r: ReadyTask, i: number) => ({
            taskId: r.id,
            title: r.title,
            start: Date.now() + (i + 1) * HOUR,
            end: Date.now() + (i + 1) * HOUR + r.estimate * 60_000,
            reason: r.priority === 'urgent' ? 'Urgent' : '',
            estimate: r.estimate,
            checked: true,
          })),
          unplaced: [],
          explanation: '',
          usedAi: false,
          aiError: '',
          by: '',
          logId: '',
        }),
      400,
    )
  app.acceptPlan = (_b: unknown, _p: unknown, cb: any) => reply(cb, true)
  app.dismissPlan = () => {}
  app.moveSchedule = (_id: string, _start: number, cb: any) => reply(cb, true)
  app.eventDetail = (_id: string, cb: any) => reply(cb, {})
  app.saveEvent = (_e: unknown, cb: any) => reply(cb, id())

  const shell: any = {
    platform: 'linux',
    colorScheme: window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light',
    accent: '#3584e4',
    sandboxed: false,
    colorSchemeChanged: signal(),
    hideWindow: () => {},
    quit: () => {},
  }
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
    shell.colorScheme = e.matches ? 'dark' : 'light'
    shell.colorSchemeChanged.emit()
  })
  const params = new URLSearchParams(location.search)
  if (params.get('platform')) shell.platform = params.get('platform')
  if (params.get('scheme')) shell.colorScheme = params.get('scheme')

  // Insight and reflection pages: small, plausible numbers.
  const dayLabels = Array.from({ length: 14 }, (_, i) => new Date(Date.now() - (13 - i) * 24 * HOUR))
  app.categories = app.categories.map((c: any) => ({ ...c, days: [0, 1, 2, 3, 4, 5, 6].map((d) => ({ day: d, enabled: d > 0 && d < 6, start: '09:00', end: '17:00' })) }))
  app.energyHistory = (_d: number, cb: any) =>
    reply(cb, {
      energy: [{ at: Date.now() - 2 * HOUR, level: 6, label: 'OK' }],
      days: dayLabels.map((d, i) => ({ date: d.toISOString().slice(0, 10), label: d.toLocaleDateString(undefined, { weekday: 'short', day: 'numeric' }), energy: 4 + ((i * 7) % 6), sleep: 60 + ((i * 13) % 35) })),
    })
  app.healthToday = (cb: any) => reply(cb, { sleep: 78 })
  app.saveHealth = (_h: unknown, cb: any) => reply(cb, true)
  app.stats = (_d: number, cb: any) =>
    reply(cb, {
      workedMinutes: 494, sessions: 16, averageSessionMinutes: 31, longestSessionMinutes: 65, finishedTasks: 8, finishedRoutines: 3, weekly: false,
      bars: dayLabels.slice(7).map((d, i) => ({ label: d.toLocaleDateString(undefined, { weekday: 'short' }), workedMinutes: [0, 60, 25, 0, 120, 80, 140][i], finished: [0, 1, 1, 0, 3, 1, 2][i] })),
      hours: Array.from({ length: 24 }, (_, h) => ({ hour: h, minutes: h >= 8 && h <= 17 ? ((h * 37) % 60) + 10 : 0, energy: h === 10 ? 7 : h === 15 ? 5 : null })),
      tasks: [{ id: 't1', title: 'Review the budget', estimate: 45, worked: 101, sessions: 2, finished: true }],
      estimates: { tasks: 8, ratio: 1.5, bySize: [{ size: '60', tasks: 6, ratio: 1.5 }], byCategory: [] },
      moving: [], planWithPace: true, paceActive: true,
    })
  app.closeOut = (cb: any) =>
    reply(cb, {
      date: app.today.date, dateLabel: app.today.dateLabel, workedMinutes: 95,
      wins: [{ taskId: 'x', title: 'Morning meds', at: Date.now() - 6 * HOUR, routine: true, workedMinutes: 0 }],
      progress: [{ taskId: 'y', title: 'Deep work: API design', minutes: 70 }],
      leftovers: app.today.overdue.map((o: AgendaItem) => ({ kind: 'block', id: o.id, taskId: o.taskId, title: o.title, start: o.start, end: o.end, timeless: false, earlier: false, dayLabel: '', bumps: o.bumpedCount })),
      tomorrow: { date: '', label: 'Tomorrow', blocks: 2, routines: 1, events: 1, firstTitle: 'Standup', firstAt: Date.now() + 18 * HOUR },
      noteId: '', note: '', closed: false,
    })
  app.weekReview = (cb: any) =>
    reply(cb, {
      rangeLabel: 'This week', workedMinutes: 494, sessions: 16, finishedTasks: 8, finishedRoutines: 3,
      days: dayLabels.slice(7).map((d, i) => ({ label: d.toLocaleDateString(undefined, { weekday: 'short' }), workedMinutes: [0, 60, 25, 0, 120, 80, 140][i], finished: [0, 1, 1, 0, 3, 1, 2][i] })),
      wins: [], moving: [{ taskId: 'm', title: 'Sort the photo backup', bumps: 3 }], stale: [], dueSoon: [], dayNotes: [], doneToday: false,
    })
  app.weekSummaryReady = signal()
  app.aiCaptureReady = signal()

  // Settings: sync and the phone companion, enough to draw them.
  const sync: any = {
    state: { syncing: false, feeds: [{ id: 'f1', name: 'Work', url: 'https://example.com/work.ics', color: '#0ea5e9', enabled: true, lastSync: Date.now(), count: 42 }], linear: { enabled: false } },
    stateChanged: signal(),
    feedAdded: signal(),
    feedFailed: signal(),
    deviceCalendarsChanged: signal(),
    deviceCalendars: (cb: any) => reply(cb, { supported: shell.platform === 'mac', access: 'notAsked', calendars: [] }),
  }
  const remote: any = {
    state: {
      enabled: true,
      running: true,
      listen: '127.0.0.1:7789',
      publicUrl: 'https://desk.example-tailnet.ts.net:7789',
      pairUrl: 'https://desk.example-tailnet.ts.net:7789/#pair=0123456789abcdef0123456789abcdef',
      secure: true,
      phones: [{ endpoint: 'e1', label: 'Pixel 8', addedAt: Date.now() }],
      streams: 1,
      serveCommand: 'tailscale serve --bg --https=7789 http://127.0.0.1:7789',
      notify: 'away',
    },
    stateChanged: signal(),
  }
  app.ollamaModels = []
  app.ollamaModelsChanged = signal()
  app.dataPath = '/home/you/.local/share/task-tumbler/tasks.db'
  app.setSetting = (key: string, value: unknown, cb: any) => {
    const map: Record<string, string> = { appearance: 'appearance', close_to_tray: 'closeToTray', autostart: 'autostart', notifications: 'notifications' }
    app.settings = { ...app.settings, [map[key] ?? key]: value }
    app.settingsChanged.emit()
    if (key === 'appearance') {
      shell.colorScheme = value === 'system' ? (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light') : value
      shell.colorSchemeChanged.emit()
    }
    reply(cb, true)
  }

  return { app, shell, sync, remote }
}
