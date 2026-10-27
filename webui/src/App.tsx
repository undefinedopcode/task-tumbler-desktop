import * as Tooltip from '@radix-ui/react-tooltip'
import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { call, useProp, useSignal } from './bridge/hooks'
import { pageTitles, Sidebar, type Page } from './components/Sidebar'
import { ApprovalDialog } from './components/ApprovalDialog'
import { EventDialog } from './components/EventDialog'
import { PlanDialog } from './components/PlanDialog'
import { isoDate } from './components/DatePicker'
import { TaskDetailPanel } from './components/TaskDetailPanel'
import { Toasts } from './components/Toasts'
import { NavContext, type Nav } from './lib/nav'
import { isMod, usePlatform } from './lib/platform'
import { Placeholder } from './pages/Placeholder'
import { SchedulePage } from './pages/SchedulePage'
import { TasksPage } from './pages/TasksPage'
import { TodayPage } from './pages/TodayPage'

function initialPage(): Page {
  const hash = location.hash.slice(1) as Page
  return hash in pageTitles ? hash : 'today'
}

export function App() {
  const [page, setPage] = useState<Page>(initialPage)
  const platform = usePlatform()
  const scheme = useProp('shell', 'colorScheme', 'light')
  const captureRef = useRef<HTMLInputElement>(null)
  const [openTask, setOpenTask] = useState<string | null>(null)
  const [plan, setPlan] = useState<string | null>(null)
  const [event, setEvent] = useState<{ id: string; start?: number } | null>(null)

  const showPage = useCallback((p: string) => {
    if (p in pageTitles) {
      setPage(p as Page)
      history.replaceState(null, '', `#${p}`)
    }
  }, [])

  useEffect(() => {
    document.documentElement.dataset.theme = scheme
    document.documentElement.dataset.platform = platform
  }, [scheme, platform])

  // The host's way in: notifications, the tray and screenshot runs.
  useEffect(() => {
    window.tumbler = { showPage, openTask: setOpenTask, openPlan: (d?: string) => setPlan(d ?? isoDate(new Date())) }
  }, [showPage])
  useSignal('app', 'pageRequested', showPage)
  useSignal('app', 'openTaskRequested', setOpenTask)
  const closeTask = useCallback(() => setOpenTask(null), [])
  const nav = useMemo<Nav>(
    () => ({
      openTask: setOpenTask,
      // Notes aren't in the web UI yet: the page, for now.
      openNote: () => showPage('notes'),
      showPage,
      openPlan: (day?: string) => setPlan(day ?? isoDate(new Date())),
      editEvent: (id: string, start?: number) => setEvent({ id, start }),
    }),
    [showPage],
  )

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const typing = e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement
      if (isMod(e, platform) && e.key.toLowerCase() === 'z' && !typing) {
        e.preventDefault()
        call('app', e.shiftKey ? 'redo' : 'undo')
      } else if (isMod(e, platform) && e.key.toLowerCase() === 'n') {
        e.preventDefault()
        showPage('today')
        setTimeout(() => captureRef.current?.focus(), 0)
      } else if (isMod(e, platform) && /^[1-4]$/.test(e.key)) {
        e.preventDefault()
        showPage((['today', 'tasks', 'schedule', 'notes'] as Page[])[Number(e.key) - 1])
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [platform, showPage])

  return (
    <NavContext.Provider value={nav}>
      <Tooltip.Provider>
        <div className="flex h-full">
          <Sidebar page={page} onPage={showPage} />
          <main className="min-w-0 flex-1 overflow-y-auto">
            {page === 'today' ? (
              <TodayPage ref={captureRef} onPage={showPage} />
            ) : page === 'tasks' ? (
              <TasksPage selectedTask={openTask} />
            ) : page === 'schedule' ? (
              <SchedulePage />
            ) : (
              <Placeholder title={pageTitles[page]} />
            )}
          </main>
          {openTask && <TaskDetailPanel key={openTask} taskId={openTask} onClose={closeTask} />}
        </div>
        <Toasts />
        <ApprovalDialog />
        {plan && <PlanDialog day={plan} onClose={() => setPlan(null)} />}
        {event && <EventDialog id={event.id} start={event.start} onClose={() => setEvent(null)} />}
      </Tooltip.Provider>
    </NavContext.Provider>
  )
}
