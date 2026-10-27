import { CalendarPlus, Check, Maximize2, Minimize2, Pause, Play, X } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { call, setProp, useProp, useQuery } from '../bridge/hooks'
import type { TaskDetail, Working } from '../bridge/types'
import { duration, elapsed } from '../lib/format'
import { useNav } from '../lib/nav'
import { ActionIcon } from './TaskDetailPanel'
import { Button, Checkbox, IconButton } from './ui'

// One task, nothing else: big title, a running clock, its steps, the things
// it opens. Do Not Disturb is on while this shows (the app does that).
export function FocusView() {
  const nav = useNav()
  const working = useProp<Working>('app', 'working', {})
  const fullScreen = useProp<boolean>('shell', 'fullScreen', false)
  const last = useRef(working.taskId ?? '')
  if (working.taskId) last.current = working.taskId
  const taskId = working.taskId ?? last.current
  const detail = useQuery<TaskDetail | null>('app', 'taskDetail', [taskId], null)
  const [now, setNow] = useState(Date.now())
  const wasFull = useRef(fullScreen)
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(t)
  }, [])
  const leave = () => {
    if (!wasFull.current) call('shell', 'setFullScreen', false)
    setProp('app', 'focusMode', false)
  }
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && leave()
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  })
  const finished = detail?.status === 'completed'

  return (
    <div className="fixed inset-0 z-40 flex animate-[fade-in_200ms_ease-out] items-center justify-center bg-bg">
      <div className="absolute top-4 right-4 flex gap-1">
        <IconButton label={fullScreen ? 'Exit full screen' : 'Full screen'} onClick={() => setProp('shell', 'fullScreen', !fullScreen)}>
          {fullScreen ? <Minimize2 className="size-4" /> : <Maximize2 className="size-4" />}
        </IconButton>
        <IconButton label="Leave focus (Esc)" onClick={leave}>
          <X className="size-4" />
        </IconButton>
      </div>
      <div className="w-full max-w-[640px] px-8">
        <div className={`text-[13px] font-semibold tracking-wide uppercase ${working.taskId ? 'text-success' : 'text-subtle'}`}>
          {working.taskId ? 'Focusing' : finished ? 'Done' : 'Paused'}
        </div>
        <h1 className="mt-2 text-[34px] leading-tight font-semibold tracking-tight text-fg">{detail?.title || 'Nothing to focus on'}</h1>
        {working.startedAt && (
          <div className="mt-4 text-[72px] leading-none font-light tracking-tight text-fg tabular-nums">{elapsed(working.startedAt, now)}</div>
        )}
        {detail?.estimate && working.startedAt ? <div className="mt-2 text-[14px] text-muted">Estimated {duration(detail.estimate)}</div> : null}
        {(detail?.subtasks.length ?? 0) > 0 && (
          <div className="mt-8 flex flex-col gap-1">
            {detail!.subtasks.map((s) => (
              <label key={s.id} className="flex min-h-10 items-center gap-3 text-[16px]">
                <Checkbox checked={s.status === 'completed'} onChange={(c) => call('app', 'setTaskStatus', s.id, c ? 'completed' : 'pending')} />
                <span className={s.status === 'completed' ? 'text-subtle line-through' : 'text-fg'}>{s.title}</span>
              </label>
            ))}
          </div>
        )}
        {(detail?.actions.length ?? 0) > 0 && (
          <div className="mt-6 flex flex-wrap gap-2">
            {detail!.actions.map((a) => (
              <Button key={a.id} icon={<ActionIcon action={a} />} onClick={() => call('app', 'runAction', a.id)}>
                {a.label}
              </Button>
            ))}
          </div>
        )}
        {detail?.description && <p className="mt-6 text-[14px] leading-relaxed whitespace-pre-line text-muted select-text">{detail.description}</p>}
        <div className="mt-10 flex gap-3">
          {!finished && taskId && (
            <Button variant="primary" icon={<Check className="size-4" />} onClick={() => call('app', 'setTaskStatus', taskId, 'completed')}>
              Done
            </Button>
          )}
          {working.taskId && (
            <Button icon={<Pause className="size-4" />} onClick={() => call('app', 'stopWork')}>
              Pause
            </Button>
          )}
          {!working.taskId && !finished && taskId && (
            <Button icon={<Play className="size-4" />} onClick={() => call('app', 'startWork', taskId)}>
              Resume
            </Button>
          )}
          {finished && (
            <Button
              variant="primary"
              icon={<CalendarPlus className="size-4" />}
              onClick={() => {
                leave()
                nav.openPlan()
              }}
            >
              Plan the next thing
            </Button>
          )}
        </div>
      </div>
    </div>
  )
}
