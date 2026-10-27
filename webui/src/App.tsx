import * as Tooltip from '@radix-ui/react-tooltip'
import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { call, useProp, useSignal } from './bridge/hooks'
import { ApprovalDialog } from './components/ApprovalDialog'
import { isoDate } from './components/DatePicker'
import { EventDialog } from './components/EventDialog'
import { FocusView } from './components/FocusView'
import { PlanDialog } from './components/PlanDialog'
import { pageTitles, Sidebar, type Page } from './components/Sidebar'
import { TaskDetailPanel } from './components/TaskDetailPanel'
import { Toasts } from './components/Toasts'
import { NavContext, type Nav } from './lib/nav'
import { isMod, usePlatform } from './lib/platform'
import { CaptureView } from './pages/CaptureView'
import { CategoriesPage } from './pages/CategoriesPage'
import { CloseOutPage } from './pages/CloseOutPage'
import { EnergyPage } from './pages/EnergyPage'
import { NotesPage } from './pages/NotesPage'
import { OnboardingPage } from './pages/OnboardingPage'
import { SchedulePage } from './pages/SchedulePage'
import { SettingsPage } from './pages/SettingsPage'
import { StatsPage } from './pages/StatsPage'
import { TasksPage } from './pages/TasksPage'
import { TodayPage } from './pages/TodayPage'
import { WeekReviewPage } from './pages/WeekReviewPage'

function initialPage(): Page {
  const hash = location.hash.slice(1) as Page
  return hash in pageTitles ? hash : 'today'
}

function useTheme() {
  const platform = usePlatform()
  const scheme = useProp('shell', 'colorScheme', 'light')
  useEffect(() => {
    document.documentElement.dataset.theme = scheme
    document.documentElement.dataset.platform = platform
  }, [scheme, platform])
}

function isTyping(el: EventTarget | Element | null) {
  return el instanceof HTMLInputElement || el instanceof HTMLTextAreaElement || (el instanceof HTMLElement && el.isContentEditable)
}

export function App() {
  // The quick-capture window loads this same page at #capture.
  return location.hash === '#capture' ? <CaptureApp /> : <MainApp />
}

function CaptureApp() {
  useTheme()
  return (
    <Tooltip.Provider>
      <CaptureView />
    </Tooltip.Provider>
  )
}

function MainApp() {
  useTheme()
  const [page, setPage] = useState<Page>(initialPage)
  const platform = usePlatform()
  const needsOnboarding = useProp<boolean>('app', 'needsOnboarding', false)
  const focusMode = useProp<boolean>('app', 'focusMode', false)
  const captureRef = useRef<HTMLInputElement>(null)
  const [openTask, setOpenTask] = useState<string | null>(null)
  const [openNote, setOpenNote] = useState<string | null>(null)
  const [plan, setPlan] = useState<string | null>(null)
  const [event, setEvent] = useState<{ id: string; start?: number } | null>(null)

  const showPage = useCallback((p: string) => {
    if (p in pageTitles) {
      setPage(p as Page)
      history.replaceState(null, '', `#${p}`)
    }
  }, [])
  const newTask = useCallback(() => {
    showPage('today')
    setTimeout(() => captureRef.current?.focus(), 0)
  }, [showPage])
  const newNote = useCallback(async () => {
    const id = await call<string>('app', 'createNote', {})
    if (id) {
      setOpenTask(null)
      setOpenNote(id)
      showPage('notes')
    }
  }, [showPage])
  // Undo in a text field is the field's; elsewhere the app's.
  const undo = useCallback((redo: boolean) => {
    if (isTyping(document.activeElement)) document.execCommand(redo ? 'redo' : 'undo')
    else call('app', redo ? 'redo' : 'undo')
  }, [])

  // The host's way in: notifications, the tray, the Mac menu bar, screenshots.
  useEffect(() => {
    window.tumbler = {
      showPage,
      openTask: setOpenTask,
      openPlan: (d?: string) => setPlan(d ?? isoDate(new Date())),
      newTask,
      newNote,
      undo: () => undo(false),
      redo: () => undo(true),
      find: () => (document.querySelector<HTMLInputElement>('main input[placeholder^="Search"]') ?? captureRef.current)?.focus(),
    }
  }, [showPage, newTask, newNote, undo])
  useSignal('app', 'pageRequested', showPage)
  useSignal('app', 'openTaskRequested', setOpenTask)
  const closeTask = useCallback(() => setOpenTask(null), [])
  const nav = useMemo<Nav>(
    () => ({
      openTask: setOpenTask,
      openNote: (id: string) => {
        // Notes have two panes of their own; the task panel would crowd them.
        setOpenTask(null)
        setOpenNote(id)
        showPage('notes')
      },
      showPage,
      openPlan: (day?: string) => setPlan(day ?? isoDate(new Date())),
      editEvent: (id: string, start?: number) => setEvent({ id, start }),
    }),
    [showPage],
  )

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!isMod(e, platform)) return
      const k = e.key.toLowerCase()
      if (k === 'z' && !isTyping(e.target)) {
        e.preventDefault()
        undo(e.shiftKey)
      } else if (k === 'n' && !e.shiftKey) {
        e.preventDefault()
        newTask()
      } else if (k === 'n' && e.shiftKey && page !== 'notes') {
        e.preventDefault()
        newNote()
      } else if (/^[1-4]$/.test(e.key)) {
        e.preventDefault()
        showPage((['today', 'tasks', 'schedule', 'notes'] as Page[])[Number(e.key) - 1])
      } else if (k === ',') {
        e.preventDefault()
        showPage('settings')
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [platform, showPage, newTask, newNote, undo, page])

  if (needsOnboarding)
    return (
      <Tooltip.Provider>
        <OnboardingPage onDone={() => showPage('today')} />
        <Toasts />
      </Tooltip.Provider>
    )

  const content = (() => {
    switch (page) {
      case 'today':
        return <TodayPage ref={captureRef} onPage={showPage} />
      case 'tasks':
        return <TasksPage selectedTask={openTask} />
      case 'schedule':
        return <SchedulePage />
      case 'notes':
        return <NotesPage noteId={openNote} onSelect={setOpenNote} />
      case 'energy':
        return <EnergyPage />
      case 'stats':
        return <StatsPage />
      case 'categories':
        return <CategoriesPage />
      case 'closeout':
        return <CloseOutPage />
      case 'review':
        return <WeekReviewPage />
      case 'settings':
        return <SettingsPage />
    }
  })()

  return (
    <NavContext.Provider value={nav}>
      <Tooltip.Provider>
        <div className="flex h-full">
          <Sidebar page={page} onPage={showPage} />
          <main className="min-w-0 flex-1 overflow-y-auto">{content}</main>
          {openTask && <TaskDetailPanel key={openTask} taskId={openTask} onClose={closeTask} />}
        </div>
        {focusMode && <FocusView />}
        <Toasts />
        <ApprovalDialog />
        {plan && <PlanDialog day={plan} onClose={() => setPlan(null)} />}
        {event && <EventDialog id={event.id} start={event.start} onClose={() => setEvent(null)} />}
      </Tooltip.Provider>
    </NavContext.Provider>
  )
}
