import * as DM from '@radix-ui/react-dropdown-menu'
import clsx from 'clsx'
import { ArrowRight, CalendarPlus, Check, ChevronLeft, ChevronRight, ExternalLink, Play, SkipForward, Sparkles, Sunrise, CalendarX2 } from 'lucide-react'
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import { call, useQuery } from '../bridge/hooks'
import type { AgendaItem, DayAgenda } from '../bridge/types'
import { DatePicker, isoDate } from '../components/DatePicker'
import { MenuContent, MenuItem, MenuSeparator } from '../components/Menu'
import { Button, Checkbox, Chip, Dot, IconButton } from '../components/ui'
import { duration, time, timeRange } from '../lib/format'
import { useNav } from '../lib/nav'

const HOUR = 56 // px per hour
const PX_PER_MS = HOUR / 3_600_000
const QUARTER = 15 * 60_000

interface Laid extends AgendaItem {
  lane: number
  lanes: number
}

// Side-by-side lanes for overlapping items: each takes the first free lane,
// and everything in an overlapping cluster shares the width.
function layOut(items: AgendaItem[]): Laid[] {
  const sorted = items
    .filter((i) => !i.timeless && !i.allDay)
    .map((i) => ({ ...i, lane: 0, lanes: 1 }))
    .sort((a, b) => a.start - b.start || b.end - a.end)
  let cluster: Laid[] = []
  let laneEnds: number[] = []
  let clusterEnd = 0
  const close = () => {
    cluster.forEach((i) => (i.lanes = laneEnds.length))
    cluster = []
    laneEnds = []
  }
  for (const item of sorted) {
    const end = Math.max(item.end, item.start + 30 * 60_000) // short items still take their drawn height
    if (cluster.length && item.start >= clusterEnd) close()
    let lane = laneEnds.findIndex((e) => e <= item.start)
    if (lane < 0) {
      lane = laneEnds.length
      laneEnds.push(end)
    } else laneEnds[lane] = end
    item.lane = lane
    cluster.push(item)
    clusterEnd = cluster.length === 1 ? end : Math.max(clusterEnd, end)
  }
  close()
  return sorted
}

const hourFmt = new Intl.DateTimeFormat(undefined, { hour: 'numeric' })
// "9 am" or "09", as the locale writes hours.
function hourLabel(h: number): string {
  return hourFmt.format(new Date(2000, 0, 1, h))
}

function shift(iso: string, days: number): string {
  const d = new Date(`${iso}T00:00:00`)
  d.setDate(d.getDate() + days)
  return isoDate(d)
}

export function SchedulePage() {
  const [day, setDay] = useState(() => isoDate(new Date()))
  const agenda = useQuery<DayAgenda | null>('app', 'dayAgenda', [day], null, ['changed', 'todayChanged'])
  const nav = useNav()
  const scroller = useRef<HTMLDivElement>(null)
  const [now, setNow] = useState(Date.now())
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 30_000)
    return () => clearInterval(t)
  }, [])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!e.altKey) return
      if (e.key === 'ArrowLeft') setDay((d) => shift(d, -1))
      if (e.key === 'ArrowRight') setDay((d) => shift(d, 1))
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])

  // Start an hour before now, or at 7:00 on other days.
  const positionedFor = useRef('')
  useLayoutEffect(() => {
    if (!agenda || !scroller.current || positionedFor.current === agenda.date) return
    positionedFor.current = agenda.date
    const y = agenda.isToday ? (Date.now() - agenda.dayStart) * PX_PER_MS - HOUR : HOUR * 7
    scroller.current.scrollTop = Math.max(0, y)
  }, [agenda])

  const laid = useMemo(() => layOut(agenda?.items ?? []), [agenda])
  const anytime = (agenda?.items ?? []).filter((i) => i.timeless)
  if (!agenda) return null
  const yFor = (ms: number) => (ms - agenda.dayStart) * PX_PER_MS

  const newEvent = () => {
    const start = new Date(`${day}T09:00:00`)
    if (agenda.isToday) {
      const n = new Date()
      start.setHours(n.getHours() + 1, 0, 0, 0)
    }
    nav.editEvent('', start.getTime())
  }

  return (
    <div className="flex h-full flex-col">
      <header className="flex flex-wrap items-end gap-3 px-8 pt-8 pb-4">
        <div className="min-w-0 flex-1">
          <div className="text-[13px] font-medium text-muted">{agenda.isToday ? 'Today' : day < isoDate(new Date()) ? 'Looking back' : 'Coming up'}</div>
          <h1 className="mt-0.5 text-[26px] leading-tight font-semibold tracking-tight text-fg">{agenda.label}</h1>
        </div>
        <div className="flex items-center gap-1.5">
          <IconButton label="Previous day (Alt+←)" onClick={() => setDay(shift(day, -1))}>
            <ChevronLeft className="size-4" />
          </IconButton>
          <Button variant="secondary" disabled={agenda.isToday} onClick={() => setDay(isoDate(new Date()))}>
            Today
          </Button>
          <IconButton label="Next day (Alt+→)" onClick={() => setDay(shift(day, 1))}>
            <ChevronRight className="size-4" />
          </IconButton>
          <DatePicker value={day} onChange={(d) => d && setDay(d)} clearable={false} quick={[{ label: 'Tomorrow', days: 1 }, { label: 'In a week', days: 7 }]} />
          <div className="mx-1 h-5 w-px bg-line" />
          <Button variant="secondary" icon={<CalendarPlus className="size-4" />} onClick={newEvent}>
            Add event
          </Button>
          {day >= isoDate(new Date()) && (
            <Button variant="primary" icon={<Sparkles className="size-4" />} onClick={() => nav.openPlan(day)}>
              Plan {agenda.isToday ? 'my day' : 'this day'}
            </Button>
          )}
        </div>
      </header>

      {agenda.allDay.length > 0 && (
        <div className="flex flex-wrap gap-1.5 px-8 pb-3">
          {agenda.allDay.map((e) => (
            <button key={e.id} onClick={() => nav.editEvent(e.id)} className="rounded-md">
              <Chip className="h-6 px-2 text-[12px]" icon={<Dot color={e.color} className="size-1.5" />}>
                {e.title}
              </Chip>
            </button>
          ))}
        </div>
      )}

      <div ref={scroller} className="relative mx-8 min-h-0 flex-1 overflow-y-auto rounded-t-xl border border-b-0 border-line bg-surface shadow-card">
        <div
          className="relative"
          style={{ height: HOUR * 24 + 16 }}
          onDoubleClick={(e) => {
            // Double-click empty time: a new event there, on the quarter hour.
            if (e.target !== e.currentTarget) return
            const rect = e.currentTarget.getBoundingClientRect()
            const ms = agenda.dayStart + (e.clientY - rect.top) / PX_PER_MS
            nav.editEvent('', agenda.dayStart + Math.floor((ms - agenda.dayStart) / QUARTER) * QUARTER)
          }}
        >
          {/* Category hours */}
          {agenda.windows.map((w, i) => (
            <div
              key={i}
              className="pointer-events-none absolute right-3 left-16 rounded-md"
              style={{ top: yFor(w.start), height: (w.end - w.start) * PX_PER_MS, background: `color-mix(in srgb, ${w.color || 'var(--tt-accent)'} 6%, transparent)` }}
            >
              <div className="absolute inset-y-0 left-0 w-[3px] rounded-full" style={{ background: `color-mix(in srgb, ${w.color || 'var(--tt-accent)'} 45%, transparent)` }} />
              <div className="absolute top-1.5 right-2 text-[11px] font-medium text-subtle">{w.name}</div>
            </div>
          ))}
          {/* Hours */}
          {Array.from({ length: 24 }, (_, h) => (
            <div key={h} className="pointer-events-none absolute right-0 left-0" style={{ top: h * HOUR }}>
              {h > 0 && (
                <div className="absolute -top-2 left-0 w-[52px] text-right text-[11px] whitespace-nowrap text-subtle tabular-nums">{hourLabel(h)}</div>
              )}
              <div className="absolute right-3 left-16 border-t border-line" />
            </div>
          ))}
          {/* Items */}
          <div className="absolute inset-y-0 right-3 left-16">
            {laid.map((item) => (
              <Block key={`${item.kind}-${item.id}`} item={item} top={yFor(item.start)} dayStart={agenda.dayStart} />
            ))}
          </div>
          {/* Now */}
          {agenda.isToday && (
            <div className="pointer-events-none absolute right-3 left-14 z-20" style={{ top: yFor(now) }}>
              <div className="h-[2px] bg-danger" />
              <div className="absolute -top-[4px] -left-[4px] size-[10px] rounded-full bg-danger" />
            </div>
          )}
        </div>
      </div>

      {anytime.length > 0 && (
        <div className="mx-8 flex flex-wrap items-center gap-x-4 gap-y-2 rounded-b-xl border border-line bg-raised px-4 py-2.5">
          <span className="text-[12.5px] font-medium text-muted">Any time {agenda.isToday ? 'today' : 'this day'}:</span>
          {anytime.map((i) => (
            <label key={i.id} className="flex items-center gap-2 text-[13px] text-fg">
              <Checkbox checked={i.state === 'done'} onChange={(c) => call('app', 'setOccurrence', i.id, c ? 'completed' : 'pending')} />
              <span className={clsx(i.state === 'done' && 'text-subtle line-through')}>{i.title}</span>
            </label>
          ))}
        </div>
      )}
      <div className="h-6 shrink-0" />
    </div>
  )
}

// One item on the timeline. Scheduled blocks drag to a new time (snapping to
// 15 minutes); a click opens what can be done with it.
function Block({ item, top, dayStart }: { item: Laid; top: number; dayStart: number }) {
  const nav = useNav()
  const [menu, setMenu] = useState(false)
  const [dy, setDy] = useState(0)
  const drag = useRef<{ y: number; moved: boolean } | null>(null)
  const finished = item.state === 'done' || item.state === 'skipped'
  const draggable = item.kind === 'schedule' && !finished
  const isEvent = item.kind === 'event'
  useEffect(() => setDy(0), [item.start])

  const height = Math.max(22, (item.end - item.start) * PX_PER_MS - 2)
  // Where a drag of `px` lands, on the quarter hour.
  const snap = (px: number) => dayStart + Math.round((item.start + px / PX_PER_MS - dayStart) / QUARTER) * QUARTER
  const dropAt = snap(dy)
  const color = isEvent ? item.color || 'var(--tt-subtle)' : item.state === 'overdue' ? 'var(--tt-danger)' : item.color || 'var(--tt-accent)'
  const compact = height < 40
  const width = `calc(${100 / item.lanes}% - 4px)`
  const left = `calc(${(100 / item.lanes) * item.lane}% + 2px)`

  return (
    <DM.Root open={menu} onOpenChange={setMenu} modal={false}>
      <DM.Trigger asChild>
        <div
          role="button"
          tabIndex={0}
          onPointerDown={(e) => {
            // Ours, not the menu's: a click opens it, a drag moves the block.
            e.preventDefault()
            if (e.button !== 0) return
            drag.current = { y: e.clientY, moved: false }
            try {
              e.currentTarget.setPointerCapture(e.pointerId)
            } catch {
              // not a real pointer (scripted)
            }
          }}
          onPointerMove={(e) => {
            const d = drag.current
            if (!d || !draggable) return
            if (Math.abs(e.clientY - d.y) > 3) d.moved = true
            if (d.moved) setDy(e.clientY - d.y)
          }}
          onPointerUp={(e) => {
            const d = drag.current
            drag.current = null
            if (d?.moved && draggable) {
              const to = snap(e.clientY - d.y)
              if (to !== item.start) call('app', 'moveSchedule', item.id, to)
              else setDy(0)
            } else if (isEvent) nav.editEvent(item.id)
            else setMenu(true)
          }}
          onContextMenu={(e) => {
            e.preventDefault()
            if (!isEvent) setMenu(true)
          }}
          className={clsx(
            'absolute overflow-hidden rounded-lg border px-2 text-left outline-none select-none',
            'focus-visible:ring-2 focus-visible:ring-accent',
            draggable ? 'cursor-grab active:cursor-grabbing' : 'cursor-pointer',
            dy !== 0 ? 'z-30 shadow-pop' : 'z-10 hover:z-20 hover:shadow-card',
            finished && 'opacity-60',
            item.working && 'ring-2 ring-accent',
            compact ? 'flex items-center gap-2 py-0' : 'py-1.5',
          )}
          style={{
            top: top + dy,
            height,
            width,
            left,
            background: `color-mix(in srgb, ${color} ${finished ? 8 : 14}%, var(--tt-surface))`,
            borderColor: `color-mix(in srgb, ${color} 45%, transparent)`,
          }}
        >
          <div className="absolute inset-y-1 left-1 w-[3px] rounded-full" style={{ background: color }} />
          <div className={clsx('truncate pl-1.5 text-[13px] font-medium text-fg', item.state === 'done' && 'line-through')}>{item.title}</div>
          <div className={clsx('truncate pl-1.5 text-[11.5px] text-muted tabular-nums', compact && 'shrink-0')}>
            {dy !== 0 ? `Drop at ${time(dropAt)}` : compact ? time(item.start) : `${timeRange(item.start, item.end)} · ${duration(Math.round((item.end - item.start) / 60000))}`}
          </div>
        </div>
      </DM.Trigger>
      <MenuContent align="start">
        {item.kind === 'schedule' && !finished && (
          <MenuItem icon={<Play />} onSelect={() => call('app', 'startWork', item.taskId)}>
            Start working
          </MenuItem>
        )}
        {!finished && (
          <MenuItem icon={<Check />} onSelect={() => (item.kind === 'schedule' ? call('app', 'completeSchedule', item.id) : call('app', 'setOccurrence', item.id, 'completed'))}>
            Done
          </MenuItem>
        )}
        {item.kind === 'schedule' && !finished && (
          <>
            <MenuItem icon={<ArrowRight />} onSelect={() => call('app', 'bumpToNextSlot', item.id)}>
              Next free slot
            </MenuItem>
            <MenuItem icon={<Sunrise />} onSelect={() => call('app', 'bumpToTomorrow', item.id)}>
              Tomorrow
            </MenuItem>
          </>
        )}
        {item.kind === 'occurrence' && !finished && (
          <MenuItem icon={<SkipForward />} onSelect={() => call('app', 'setOccurrence', item.id, 'skipped')}>
            Skip
          </MenuItem>
        )}
        {item.kind === 'schedule' && (
          <MenuItem icon={<CalendarX2 />} onSelect={() => call('app', 'unschedule', item.id)}>
            Remove from schedule
          </MenuItem>
        )}
        <MenuSeparator />
        <MenuItem icon={<ExternalLink />} onSelect={() => nav.openTask(item.taskId)}>
          Open task
        </MenuItem>
      </MenuContent>
    </DM.Root>
  )
}
