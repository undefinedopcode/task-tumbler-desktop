import * as DM from '@radix-ui/react-dropdown-menu'
import clsx from 'clsx'
import { ArrowRight, CalendarPlus, Check, ChevronLeft, ChevronRight, ExternalLink, Play, SkipForward, Sparkles, Sunrise, CalendarX2 } from 'lucide-react'
import { useEffect, useLayoutEffect, useMemo, useRef, useState, type ReactNode, type RefObject } from 'react'
import { call, useQuery } from '../bridge/hooks'
import type { AgendaItem, DayAgenda } from '../bridge/types'
import { DatePicker, isoDate } from '../components/DatePicker'
import { MenuContent, MenuItem, MenuSeparator } from '../components/Menu'
import { Segmented } from '../components/Segmented'
import { Button, Checkbox, Chip, Dot, IconButton } from '../components/ui'
import { duration, time, timeRange } from '../lib/format'
import { useNav } from '../lib/nav'

const HOUR = 56 // px per hour
const PX_PER_MS = HOUR / 3_600_000
const QUARTER = 15 * 60_000
const REFRESH = ['changed', 'todayChanged']

type View = 'day' | 'week'
const VIEW_KEY = 'tt.scheduleView'

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

// The Monday of the week `iso` falls in (weeks start on Monday, as in the date picker).
function weekStart(iso: string): string {
  const d = new Date(`${iso}T00:00:00`)
  return shift(iso, -((d.getDay() + 6) % 7))
}

// The same time of day, `days` later: right across a clock change.
function shiftMs(ms: number, days: number): number {
  const d = new Date(ms)
  d.setDate(d.getDate() + days)
  return d.getTime()
}

const rangeFmt = new Intl.DateTimeFormat(undefined, { day: 'numeric', month: 'short' })
const weekdayFmt = new Intl.DateTimeFormat(undefined, { weekday: 'short' })
const dayLongFmt = new Intl.DateTimeFormat(undefined, { weekday: 'short', day: 'numeric', month: 'short' })

function readView(): View {
  try {
    return localStorage.getItem(VIEW_KEY) === 'week' ? 'week' : 'day'
  } catch {
    return 'day'
  }
}

function useNow(): number {
  const [now, setNow] = useState(Date.now())
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 30_000)
    return () => clearInterval(t)
  }, [])
  return now
}

// Start an hour before now when it's in view, or at 7:00.
function useStartScroll(scroller: RefObject<HTMLDivElement | null>, key: string, y: number | null) {
  const positionedFor = useRef('')
  useLayoutEffect(() => {
    if (y === null || !scroller.current || positionedFor.current === key) return
    positionedFor.current = key
    scroller.current.scrollTop = Math.max(0, y)
  }, [scroller, key, y])
}

export function SchedulePage() {
  const [day, setDay] = useState(() => isoDate(new Date()))
  const [view, setViewState] = useState<View>(readView)
  const setView = (v: View) => {
    setViewState(v)
    try {
      localStorage.setItem(VIEW_KEY, v)
    } catch {
      // not kept; fine
    }
  }
  const step = view === 'week' ? 7 : 1

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!e.altKey) return
      if (e.key === 'ArrowLeft') setDay((d) => shift(d, -step))
      if (e.key === 'ArrowRight') setDay((d) => shift(d, step))
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [step])

  const nav = { day, setDay, view, setView }
  return view === 'week' ? <WeekView {...nav} /> : <DayView {...nav} />
}

interface NavProps {
  day: string
  setDay: (d: string) => void
  view: View
  setView: (v: View) => void
}

function Header({ day, setDay, view, setView, kicker, title, current, children }: NavProps & { kicker: string; title: string; current: boolean; children?: ReactNode }) {
  const step = view === 'week' ? 7 : 1
  const unit = view === 'week' ? 'week' : 'day'
  return (
    <header className="flex flex-wrap items-end gap-3 px-8 pt-8 pb-4">
      <div className="min-w-0 flex-1">
        <div className="text-[13px] font-medium text-muted">{kicker}</div>
        <h1 className="mt-0.5 text-[26px] leading-tight font-semibold tracking-tight text-fg">{title}</h1>
      </div>
      <div className="flex items-center gap-1.5">
        <Segmented
          value={view}
          onChange={setView}
          options={[
            { value: 'day', label: 'Day' },
            { value: 'week', label: 'Week' },
          ]}
        />
        <div className="mx-1 h-5 w-px bg-line" />
        <IconButton label={`Previous ${unit} (Alt+←)`} onClick={() => setDay(shift(day, -step))}>
          <ChevronLeft className="size-4" />
        </IconButton>
        <Button variant="secondary" disabled={current} onClick={() => setDay(isoDate(new Date()))}>
          {view === 'week' ? 'This week' : 'Today'}
        </Button>
        <IconButton label={`Next ${unit} (Alt+→)`} onClick={() => setDay(shift(day, step))}>
          <ChevronRight className="size-4" />
        </IconButton>
        <DatePicker value={day} onChange={(d) => d && setDay(d)} clearable={false} quick={[{ label: 'Tomorrow', days: 1 }, { label: 'In a week', days: 7 }]} />
        <div className="mx-1 h-5 w-px bg-line" />
        {children}
      </div>
    </header>
  )
}

// A new event at the next hour today, or at 9:00 on another day.
function newEventStart(iso: string): number {
  const start = new Date(`${iso}T09:00:00`)
  if (iso === isoDate(new Date())) {
    const n = new Date()
    start.setHours(n.getHours() + 1, 0, 0, 0)
  }
  return start.getTime()
}

function DayView(props: NavProps) {
  const { day } = props
  const agenda = useQuery<DayAgenda | null>('app', 'dayAgenda', [day], null, REFRESH)
  const nav = useNav()
  const scroller = useRef<HTMLDivElement>(null)
  const now = useNow()
  useStartScroll(scroller, `day-${agenda?.date}`, agenda ? (agenda.isToday ? (Date.now() - agenda.dayStart) * PX_PER_MS - HOUR : HOUR * 7) : null)

  if (!agenda) return null
  const anytime = agenda.items.filter((i) => i.timeless)

  return (
    <div className="flex h-full flex-col">
      <Header {...props} kicker={agenda.isToday ? 'Today' : day < isoDate(new Date()) ? 'Looking back' : 'Coming up'} title={agenda.label} current={agenda.isToday}>
        <Button variant="secondary" icon={<CalendarPlus className="size-4" />} onClick={() => nav.editEvent('', newEventStart(day))}>
          Add event
        </Button>
        {day >= isoDate(new Date()) && (
          <Button variant="primary" icon={<Sparkles className="size-4" />} onClick={() => nav.openPlan(day)}>
            Plan {agenda.isToday ? 'my day' : 'this day'}
          </Button>
        )}
      </Header>

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
        <HourGrid>
          <DayColumn agenda={agenda} now={now} />
        </HourGrid>
      </div>

      {anytime.length > 0 && (
        <div className="mx-8 flex flex-wrap items-center gap-x-4 gap-y-2 rounded-b-xl border border-line bg-raised px-4 py-2.5">
          <span className="text-[12.5px] font-medium text-muted">Any time {agenda.isToday ? 'today' : 'this day'}:</span>
          {anytime.map((i) => (
            <AnytimeItem key={i.id} item={i} />
          ))}
        </div>
      )}
      <div className="h-6 shrink-0" />
    </div>
  )
}

function WeekView(props: NavProps) {
  const first = weekStart(props.day)
  const days = Array.from({ length: 7 }, (_, i) => shift(first, i))
  // Seven fixed queries, one per day, so each refreshes on its own.
  const agendas = [
    useQuery<DayAgenda | null>('app', 'dayAgenda', [days[0]], null, REFRESH),
    useQuery<DayAgenda | null>('app', 'dayAgenda', [days[1]], null, REFRESH),
    useQuery<DayAgenda | null>('app', 'dayAgenda', [days[2]], null, REFRESH),
    useQuery<DayAgenda | null>('app', 'dayAgenda', [days[3]], null, REFRESH),
    useQuery<DayAgenda | null>('app', 'dayAgenda', [days[4]], null, REFRESH),
    useQuery<DayAgenda | null>('app', 'dayAgenda', [days[5]], null, REFRESH),
    useQuery<DayAgenda | null>('app', 'dayAgenda', [days[6]], null, REFRESH),
  ]
  const nav = useNav()
  const scroller = useRef<HTMLDivElement>(null)
  const columns = useRef<HTMLDivElement>(null)
  const [colWidth, setColWidth] = useState(0)
  const now = useNow()
  useLayoutEffect(() => {
    const el = columns.current
    if (!el) return
    const measure = () => setColWidth(el.clientWidth / 7)
    measure()
    const ro = new ResizeObserver(measure)
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  const today = isoDate(new Date())
  const ready = agendas.every((a) => a && days.includes(a.date))
  const todayAgenda = agendas.find((a) => a?.isToday)
  useStartScroll(scroller, `week-${first}`, ready ? (todayAgenda ? (Date.now() - todayAgenda.dayStart) * PX_PER_MS - HOUR : HOUR * 7) : null)

  const last = days[6]
  const title = rangeFmt.formatRange(new Date(`${first}T00:00:00`), new Date(`${last}T00:00:00`))
  const thisWeek = first === weekStart(today)
  const kicker = thisWeek ? 'This week' : last < today ? 'Looking back' : 'Coming up'
  const openDay = (iso: string) => {
    props.setDay(iso)
    props.setView('day')
  }

  return (
    <div className="flex h-full flex-col">
      <Header {...props} kicker={kicker} title={title} current={thisWeek}>
        <Button variant="secondary" icon={<CalendarPlus className="size-4" />} onClick={() => nav.editEvent('', newEventStart(days.includes(today) ? today : first))}>
          Add event
        </Button>
      </Header>

      <div ref={scroller} className="relative mx-8 mb-6 min-h-0 flex-1 overflow-y-auto rounded-xl border border-line bg-surface shadow-card">
        {/* Day headings, with each day's all-day and any-time items */}
        <div className="sticky top-0 z-40 flex border-b border-line bg-surface">
          <div className="w-16 shrink-0" />
          {days.map((iso, i) => (
            <WeekDayHead key={iso} iso={iso} agenda={agendas[i]?.date === iso ? agendas[i] : null} isToday={iso === today} first={i === 0} onOpen={() => openDay(iso)} />
          ))}
          <div className="w-3 shrink-0" />
        </div>
        <HourGrid columnsRef={columns}>
          {days.map((iso, i) => {
            const a = agendas[i]
            return a?.date === iso ? <DayColumn key={iso} agenda={a} now={now} narrow col={i} cols={7} colWidth={colWidth} /> : <div key={iso} className="flex-1" />
          })}
        </HourGrid>
      </div>
    </div>
  )
}

function WeekDayHead({ iso, agenda, isToday, first, onOpen }: { iso: string; agenda: DayAgenda | null; isToday: boolean; first: boolean; onOpen: () => void }) {
  const nav = useNav()
  const d = new Date(`${iso}T00:00:00`)
  const allDay = agenda?.allDay ?? []
  const anytime = (agenda?.items ?? []).filter((i) => i.timeless)
  const extra = [...allDay, ...anytime]
  const shown = extra.slice(0, 3)
  return (
    <div className={clsx('min-w-0 flex-1 px-1.5 pt-2 pb-1.5', !first && 'border-l border-line')}>
      <button onClick={onOpen} title={`Open ${dayLongFmt.format(d)}`} className="flex w-full items-baseline gap-1.5 rounded-md px-1 text-left hover:bg-hover">
        <span className={clsx('text-[12px] font-medium', isToday ? 'text-accent' : 'text-muted')}>{weekdayFmt.format(d)}</span>
        <span className={clsx('text-[17px] font-semibold tabular-nums', isToday ? 'text-accent' : 'text-fg')}>{d.getDate()}</span>
      </button>
      <div className="mt-1 flex min-h-0 flex-col gap-0.5">
        {shown.map((i) =>
          i.kind === 'event' ? (
            <button key={`e-${i.id}`} onClick={() => nav.editEvent(i.id)} className="flex min-w-0 items-center gap-1 rounded px-1 text-left text-[11.5px] text-fg hover:bg-hover">
              <Dot color={i.color} className="size-1.5 shrink-0" />
              <span className="truncate">{i.title}</span>
            </button>
          ) : (
            <label key={`o-${i.id}`} className="flex min-w-0 items-center gap-1 px-1 text-[11.5px] text-fg">
              <Checkbox checked={i.state === 'done'} onChange={(c) => call('app', 'setOccurrence', i.id, c ? 'completed' : 'pending')} />
              <span className={clsx('truncate', i.state === 'done' && 'text-subtle line-through')}>{i.title}</span>
            </label>
          ),
        )}
        {extra.length > shown.length && (
          <button onClick={onOpen} className="px-1 text-left text-[11px] text-subtle hover:text-fg">
            +{extra.length - shown.length} more
          </button>
        )}
      </div>
    </div>
  )
}

function AnytimeItem({ item }: { item: AgendaItem }) {
  return (
    <label className="flex items-center gap-2 text-[13px] text-fg">
      <Checkbox checked={item.state === 'done'} onChange={(c) => call('app', 'setOccurrence', item.id, c ? 'completed' : 'pending')} />
      <span className={clsx(item.state === 'done' && 'text-subtle line-through')}>{item.title}</span>
    </label>
  )
}

// The 24 hours, with day columns laid side by side over them.
function HourGrid({ children, columnsRef }: { children: ReactNode; columnsRef?: RefObject<HTMLDivElement | null> }) {
  return (
    <div className="relative" style={{ height: HOUR * 24 + 16 }}>
      {Array.from({ length: 24 }, (_, h) => (
        <div key={h} className="pointer-events-none absolute right-0 left-0" style={{ top: h * HOUR }}>
          {h > 0 && <div className="absolute -top-2 left-0 w-[52px] text-right text-[11px] whitespace-nowrap text-subtle tabular-nums">{hourLabel(h)}</div>}
          <div className="absolute right-3 left-16 border-t border-line" />
        </div>
      ))}
      <div ref={columnsRef} className="absolute inset-y-0 right-3 left-16 flex">
        {children}
      </div>
    </div>
  )
}

// One day's category hours, items and (today) the now line. In the week view
// it's one of seven: blocks also drag sideways to another day.
function DayColumn({ agenda, now, narrow = false, col = 0, cols = 1, colWidth = 0 }: { agenda: DayAgenda; now: number; narrow?: boolean; col?: number; cols?: number; colWidth?: number }) {
  const nav = useNav()
  const laid = useMemo(() => layOut(agenda.items), [agenda])
  const yFor = (ms: number) => (ms - agenda.dayStart) * PX_PER_MS
  return (
    <div
      className={clsx('relative min-w-0 flex-1', narrow && 'border-l border-line first:border-l-0')}
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
          className={clsx('pointer-events-none absolute inset-x-0', narrow ? 'rounded-none' : 'rounded-md')}
          style={{ top: yFor(w.start), height: (w.end - w.start) * PX_PER_MS, background: `color-mix(in srgb, ${w.color || 'var(--tt-accent)'} 6%, transparent)` }}
        >
          {!narrow && (
            <>
              <div className="absolute inset-y-0 left-0 w-[3px] rounded-full" style={{ background: `color-mix(in srgb, ${w.color || 'var(--tt-accent)'} 45%, transparent)` }} />
              <div className="absolute top-1.5 right-2 text-[11px] font-medium text-subtle">{w.name}</div>
            </>
          )}
        </div>
      ))}
      {laid.map((item) => (
        <Block key={`${item.kind}-${item.id}`} item={item} top={yFor(item.start)} dayStart={agenda.dayStart} narrow={narrow} col={col} cols={cols} colWidth={colWidth} />
      ))}
      {/* Now */}
      {agenda.isToday && (
        <div className="pointer-events-none absolute inset-x-0 z-20" style={{ top: yFor(now) }}>
          <div className="h-[2px] bg-danger" />
          <div className="absolute -top-[4px] -left-[4px] size-[10px] rounded-full bg-danger" />
        </div>
      )}
    </div>
  )
}

// One item on the timeline. Scheduled blocks drag to a new time (snapping to
// 15 minutes), and in the week view to another day; a click opens what can be
// done with it.
function Block({ item, top, dayStart, narrow, col, cols, colWidth }: { item: Laid; top: number; dayStart: number; narrow: boolean; col: number; cols: number; colWidth: number }) {
  const nav = useNav()
  const [menu, setMenu] = useState(false)
  const [dy, setDy] = useState(0)
  const [dx, setDx] = useState(0)
  const drag = useRef<{ x: number; y: number; moved: boolean } | null>(null)
  const finished = item.state === 'done' || item.state === 'skipped'
  const draggable = item.kind === 'schedule' && !finished
  const isEvent = item.kind === 'event'
  useEffect(() => {
    setDy(0)
    setDx(0)
  }, [item.start])

  const height = Math.max(22, (item.end - item.start) * PX_PER_MS - 2)
  // Where a drag of `px` lands, on the quarter hour.
  const snap = (px: number) => dayStart + Math.round((item.start + px / PX_PER_MS - dayStart) / QUARTER) * QUARTER
  // How many days sideways a drag of `px` goes, staying in the week.
  const daysFor = (px: number) => (colWidth > 0 ? Math.max(-col, Math.min(cols - 1 - col, Math.round(px / colWidth))) : 0)
  const days = daysFor(dx)
  const dropAt = shiftMs(snap(dy), days)
  const dragging = dy !== 0 || dx !== 0
  const color = isEvent ? item.color || 'var(--tt-subtle)' : item.state === 'overdue' ? 'var(--tt-danger)' : item.color || 'var(--tt-accent)'
  const compact = height < 40
  const width = `calc(${100 / item.lanes}% - ${narrow ? 2 : 4}px)`
  const left = `calc(${(100 / item.lanes) * item.lane}% + ${narrow ? 1 : 2}px)`

  return (
    <DM.Root open={menu} onOpenChange={setMenu} modal={false}>
      <DM.Trigger asChild>
        <div
          role="button"
          tabIndex={0}
          title={narrow ? `${item.title} · ${timeRange(item.start, item.end)}` : undefined}
          onPointerDown={(e) => {
            // Ours, not the menu's: a click opens it, a drag moves the block.
            e.preventDefault()
            if (e.button !== 0) return
            drag.current = { x: e.clientX, y: e.clientY, moved: false }
            try {
              e.currentTarget.setPointerCapture(e.pointerId)
            } catch {
              // not a real pointer (scripted)
            }
          }}
          onPointerMove={(e) => {
            const d = drag.current
            if (!d || !draggable) return
            if (Math.abs(e.clientY - d.y) > 3 || Math.abs(e.clientX - d.x) > 3) d.moved = true
            if (d.moved) {
              setDy(e.clientY - d.y)
              setDx(colWidth > 0 ? e.clientX - d.x : 0)
            }
          }}
          onPointerUp={(e) => {
            const d = drag.current
            drag.current = null
            if (d?.moved && draggable) {
              const to = shiftMs(snap(e.clientY - d.y), daysFor(colWidth > 0 ? e.clientX - d.x : 0))
              if (to !== item.start) call('app', 'moveSchedule', item.id, to)
              else {
                setDy(0)
                setDx(0)
              }
            } else if (isEvent) nav.editEvent(item.id)
            else setMenu(true)
          }}
          onContextMenu={(e) => {
            e.preventDefault()
            if (!isEvent) setMenu(true)
          }}
          className={clsx(
            'absolute overflow-hidden rounded-lg border text-left outline-none select-none',
            narrow ? 'px-1.5' : 'px-2',
            'focus-visible:ring-2 focus-visible:ring-accent',
            draggable ? 'cursor-grab active:cursor-grabbing' : 'cursor-pointer',
            dragging ? 'z-30 shadow-pop' : 'z-10 hover:z-20 hover:shadow-card',
            finished && 'opacity-60',
            item.working && 'ring-2 ring-accent',
            compact ? 'flex items-center gap-2 py-0' : 'py-1.5',
          )}
          style={{
            top: top + dy,
            height,
            width,
            left,
            transform: days ? `translateX(${days * colWidth}px)` : undefined,
            background: `color-mix(in srgb, ${color} ${finished ? 8 : 14}%, var(--tt-surface))`,
            borderColor: `color-mix(in srgb, ${color} 45%, transparent)`,
          }}
        >
          <div className="absolute inset-y-1 left-1 w-[3px] rounded-full" style={{ background: color }} />
          <div className={clsx('truncate pl-1.5 font-medium text-fg', narrow ? 'text-[12px]' : 'text-[13px]', item.state === 'done' && 'line-through')}>{item.title}</div>
          {(!compact || !narrow || dragging) && (
            <div className={clsx('truncate pl-1.5 text-[11.5px] text-muted tabular-nums', compact && 'shrink-0')}>
              {dragging
                ? `Drop at ${days ? `${weekdayFmt.format(dropAt)} ` : ''}${time(dropAt)}`
                : compact || narrow
                  ? time(item.start)
                  : `${timeRange(item.start, item.end)} · ${duration(Math.round((item.end - item.start) / 60000))}`}
            </div>
          )}
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
