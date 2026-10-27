import { CalendarClock, CalendarPlus, CheckCircle2, Clock, CloudSun, Moon, Square, Timer } from 'lucide-react'
import { useCallback, useEffect, useRef, useState } from 'react'
import { call, useProp, useQuery } from '../bridge/hooks'
import type { Working } from '../bridge/types'
import { Banner } from '../components/Banner'
import { Section } from '../components/settings'
import { Button, Tip } from '../components/ui'
import { duration, time } from '../lib/format'
import { useNav } from '../lib/nav'
import { keys, usePlatform } from '../lib/platform'

interface Leftover {
  kind: 'block' | 'routine'
  id: string
  taskId: string
  title: string
  start: number
  end: number
  timeless: boolean
  earlier: boolean
  dayLabel: string
  bumps: number
}
interface Day {
  date: string
  dateLabel: string
  workedMinutes: number
  wins: { taskId: string; title: string; at: number; routine: boolean; workedMinutes: number }[]
  progress: { taskId: string; title: string; minutes: number }[]
  leftovers: Leftover[]
  tomorrow: { date: string; label: string; blocks: number; routines: number; events: number; firstTitle?: string; firstAt?: number }
  noteId: string
  note: string
  closed: boolean
}

function when(l: Leftover): string {
  if (l.kind === 'routine') return l.timeless ? 'Routine, any time today' : `Routine at ${time(l.start)}`
  const at = l.earlier ? `${l.dayLabel} ${time(l.start)}` : time(l.start)
  return l.bumps > 1 ? `${at} · moved ${l.bumps} times` : at
}

// Closing out the day: what got done (a list, not a score), one tap for each
// loose end, a line to remember, and a look at tomorrow.
export function CloseOutPage() {
  const nav = useNav()
  const platform = usePlatform()
  const day = useQuery<Day | null>('app', 'closeOut', [], null)
  const working = useProp<Working>('app', 'working', {})
  const [note, setNote] = useState<string | null>(null)
  const timer = useRef<ReturnType<typeof setTimeout>>(undefined)
  const pending = useRef<string | null>(null)

  // The note once, not bound: saving refreshes `day`, which mustn't reset typing.
  useEffect(() => {
    if (day && note === null) setNote(day.note ?? '')
  }, [day, note])
  const flush = useCallback(() => {
    clearTimeout(timer.current)
    if (pending.current !== null) {
      call('app', 'saveDayNote', pending.current)
      pending.current = null
    }
  }, [])
  useEffect(() => flush, [flush])

  if (!day) return null
  const t = day.tomorrow
  const tomorrowParts = [
    t.blocks ? `${t.blocks} ${t.blocks === 1 ? 'block' : 'blocks'}` : '',
    t.events ? `${t.events} ${t.events === 1 ? 'event' : 'events'}` : '',
    t.routines ? `${t.routines} ${t.routines === 1 ? 'routine' : 'routines'}` : '',
  ].filter(Boolean)

  return (
    <div className="mx-auto flex w-full max-w-[760px] flex-col gap-8 px-8 pt-8 pb-16">
      <header>
        <div className="text-[13px] font-medium text-muted">{day.dateLabel}</div>
        <h1 className="mt-0.5 text-[26px] leading-tight font-semibold tracking-tight">Close out the day</h1>
      </header>

      <div className="rounded-2xl border border-line bg-surface p-5 shadow-card">
        <div className="flex items-center gap-3">
          {day.wins.length ? <CheckCircle2 className="size-6 text-success" /> : <CloudSun className="size-6 text-subtle" />}
          <div>
            <div className="text-[17px] font-semibold text-fg">
              {day.wins.length
                ? `You finished ${day.wins.length} ${day.wins.length === 1 ? 'thing' : 'things'} today`
                : day.progress.length
                  ? 'You moved things forward today'
                  : 'A quiet day'}
            </div>
            <div className="text-[13px] text-muted">
              {day.workedMinutes > 0 ? `${duration(day.workedMinutes)} of focused time logged.` : day.wins.length || day.progress.length ? '' : 'Some days are like that. Rest counts too.'}
            </div>
          </div>
        </div>
        {(day.wins.length > 0 || day.progress.length > 0) && (
          <div className="mt-4 flex flex-col">
            {day.wins.map((w) => (
              <button key={`w${w.taskId}${w.at}`} onClick={() => nav.openTask(w.taskId)} className="flex items-center gap-2.5 rounded-lg px-2 py-1.5 text-left hover:bg-hover">
                <CheckCircle2 className="size-4 text-success" />
                <span className="flex-1 text-[13.5px] text-fg">{w.title}</span>
                <span className="text-[12px] text-subtle">{w.routine ? 'Routine' : w.workedMinutes > 0 ? `${duration(w.workedMinutes)} in all` : ''}</span>
              </button>
            ))}
            {day.progress.map((p) => (
              <button key={`p${p.taskId}`} onClick={() => nav.openTask(p.taskId)} className="flex items-center gap-2.5 rounded-lg px-2 py-1.5 text-left hover:bg-hover">
                <Timer className="size-4 text-accent" />
                <span className="flex-1 text-[13.5px] text-fg">{p.title}</span>
                <span className="text-[12px] text-subtle">progress: {duration(p.minutes)} today</span>
              </button>
            ))}
          </div>
        )}
      </div>

      {working.id && (
        <Banner
          icon={<Clock />}
          actions={
            <Button size="sm" icon={<Square className="size-3.5" />} onClick={() => call('app', 'stopWork')}>
              Stop it
            </Button>
          }
        >
          The timer is still running on “{working.title}”.
        </Banner>
      )}

      {day.leftovers.length > 0 && (
        <Section id="open" title="Still open" description="None of these has to happen today. Pick where each one goes, or clear them all and let tomorrow's plan pick them up.">
          {day.leftovers.map((l) => (
            <div key={`${l.kind}-${l.id}`} className="flex flex-wrap items-center gap-x-4 gap-y-2 px-4 py-3">
              <button onClick={() => nav.openTask(l.taskId)} className="min-w-[200px] flex-1 text-left">
                <div className="text-[13.5px] font-medium text-fg">{l.title}</div>
                <div className={l.earlier ? 'text-[12.5px] text-warning' : 'text-[12.5px] text-muted'}>{when(l)}</div>
              </button>
              <div className="flex flex-wrap gap-1">
                {(l.kind === 'routine'
                  ? [
                      { choice: 'done', label: 'Done', tip: 'It did happen' },
                      { choice: 'skip', label: 'Skip today', tip: 'It comes round again anyway' },
                    ]
                  : [
                      { choice: 'tomorrow', label: 'Tomorrow', tip: 'The first free slot tomorrow' },
                      { choice: 'pile', label: 'Back to the pile', tip: 'Unschedule it; it stays on your list' },
                      { choice: 'drop', label: 'Let it go', tip: `Cancel the task (${keys(platform, 'Mod+Z')} to undo)` },
                      { choice: 'done', label: 'Did it', tip: 'Mark it done' },
                    ]
                ).map((o) => (
                  <Tip key={o.choice} label={o.tip}>
                    <Button size="sm" variant={o.choice === 'done' ? 'soft' : 'ghost'} onClick={() => call('app', 'resolveLeftover', l.kind, l.id, o.choice)}>
                      {o.label}
                    </Button>
                  </Tip>
                ))}
              </div>
            </div>
          ))}
          {day.leftovers.length > 1 && (
            <div className="flex items-center gap-3 px-4 py-3">
              <span className="flex-1 text-[12.5px] text-muted">Unschedule them all and skip today's routines, in one step.</span>
              <Button size="sm" onClick={() => call('app', 'clearLeftovers')}>
                Clear the rest
              </Button>
            </div>
          )}
        </Section>
      )}

      <Section id="note" title="Anything to remember about today?">
        <textarea
          value={note ?? ''}
          rows={4}
          onChange={(e) => {
            setNote(e.target.value)
            pending.current = e.target.value
            clearTimeout(timer.current)
            timer.current = setTimeout(flush, 800)
          }}
          onBlur={flush}
          placeholder="What helped, what got in the way, what's on your mind… (saved to Notes)"
          className="block w-full resize-y rounded-xl bg-transparent px-4 py-3 text-[14px] leading-relaxed text-fg outline-none placeholder:text-subtle focus-visible:outline-none"
        />
      </Section>

      <Section id="tomorrow" title="Tomorrow">
        <div className="flex items-center gap-3 px-4 py-3">
          <CalendarClock className="size-5 text-subtle" />
          <div className="flex-1">
            <div className="text-[13.5px] font-medium text-fg">{t.label}</div>
            <div className="text-[12.5px] text-muted">
              {tomorrowParts.length ? tomorrowParts.join(', ') : 'Nothing planned yet'}
              {t.firstTitle && t.firstAt ? ` · first up: ${t.firstTitle} at ${time(t.firstAt)}` : ''}
            </div>
          </div>
          <Button size="sm" icon={<CalendarPlus className="size-3.5" />} onClick={() => nav.openPlan(t.date)}>
            Plan tomorrow
          </Button>
        </div>
      </Section>

      <div className="flex items-center justify-end gap-3">
        {day.closed && <span className="text-[12.5px] text-muted">Day closed. You can come back and change things any time.</span>}
        <Button
          variant="primary"
          icon={<Moon className="size-4" />}
          onClick={() => {
            flush()
            call('app', 'finishCloseOut', false)
            nav.showPage('today')
          }}
        >
          {day.closed ? 'Close it again' : 'Close the day'}
        </Button>
      </div>
    </div>
  )
}
