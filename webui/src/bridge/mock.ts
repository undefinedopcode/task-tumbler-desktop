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

  return { app, shell }
}
