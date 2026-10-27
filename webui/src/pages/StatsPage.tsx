import clsx from 'clsx'
import { ArrowUpRight } from 'lucide-react'
import { useMemo, useState } from 'react'
import { useQuery } from '../bridge/hooks'
import { BarChart } from '../components/BarChart'
import { Segmented } from '../components/Segmented'
import { Section } from '../components/settings'
import { Empty } from '../components/ui'
import { duration, time } from '../lib/format'
import { useNav } from '../lib/nav'
import { BarChart3 } from 'lucide-react'

interface Stats {
  workedMinutes: number
  sessions: number
  averageSessionMinutes: number
  longestSessionMinutes: number
  finishedTasks: number
  finishedRoutines: number
  bars: { label: string; workedMinutes: number; finished: number }[]
  weekly: boolean
  hours: { hour: number; minutes: number; energy?: number | null }[]
  tasks: { id: string; title: string; estimate: number; worked: number; sessions: number; finished: boolean }[]
  estimates: { tasks: number; ratio: number; bySize: { size: string; tasks: number; ratio: number }[]; byCategory: { name: string; tasks: number; ratio: number }[] }
  moving: { id: string; title: string; bumps: number }[]
  planWithPace: boolean
  paceActive: boolean
}

const sizeText: Record<string, string> = {
  '15': 'Quick tasks (up to 15 min)',
  '30': 'Up to half an hour',
  '60': 'Up to an hour',
  '120': 'Up to two hours',
  longer: 'Longer than two hours',
}
const ratio = (r: number) => `${r.toFixed(1)}×`
const ratioTone = (r: number) => (r > 1.25 ? 'text-warning' : r < 0.8 ? 'text-success' : 'text-fg')
const hourText = (h: number) => time(new Date(2000, 0, 1, h).getTime())
const hourAxis = new Intl.DateTimeFormat(undefined, { hour: 'numeric' })

// Looking back: time worked, things finished, the hours that work for you,
// and how long things take against your guesses. Not a score; for planning.
export function StatsPage() {
  const nav = useNav()
  const [days, setDays] = useState('7')
  const s = useQuery<Stats | null>('app', 'stats', [Number(days)], null)
  const [allTasks, setAllTasks] = useState(false)

  const hourRange = useMemo(() => {
    if (!s) return []
    let first = 8
    let last = 19
    for (const h of s.hours)
      if (h.minutes > 0 || h.energy != null) {
        first = Math.min(first, h.hour)
        last = Math.max(last, h.hour)
      }
    return s.hours.slice(first, last + 1)
  }, [s])
  const bestHour = useMemo(() => {
    if (!s) return -1
    let best = -1
    let most = 0
    for (let h = 0; h < 24; h++) {
      const sum = s.hours[h].minutes + s.hours[(h + 1) % 24].minutes
      if (sum > most) {
        most = sum
        best = h
      }
    }
    return most >= 30 ? best : -1
  }, [s])

  if (!s) return null
  const empty = s.workedMinutes === 0 && s.finishedTasks === 0 && s.finishedRoutines === 0
  const e = s.estimates

  return (
    <div className="mx-auto flex w-full max-w-[880px] flex-col gap-8 px-8 pt-8 pb-16">
      <header className="flex items-end gap-4">
        <div className="flex-1">
          <h1 className="text-[26px] leading-tight font-semibold tracking-tight">Stats</h1>
          <p className="mt-1 text-[13.5px] text-muted">Nothing here is a score; it's for planning better.</p>
        </div>
        <Segmented
          value={days}
          onChange={setDays}
          options={[
            { value: '7', label: 'Week' },
            { value: '30', label: 'Month' },
            { value: '90', label: '3 months' },
            { value: '0', label: 'All time' },
          ]}
        />
      </header>

      {empty ? (
        <Empty
          icon={<BarChart3 className="size-6" />}
          title="Nothing here yet"
          hint={days === '0' ? 'Start the timer when you work on a task, and finish a few. This fills in as you go.' : 'No work logged or tasks finished in this time. Try a longer range.'}
        />
      ) : (
        <>
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-3">
            {[
              { label: 'Time worked', value: duration(s.workedMinutes), detail: `in ${s.sessions} ${s.sessions === 1 ? 'session' : 'sessions'}` },
              { label: 'Finished', value: `${s.finishedTasks} ${s.finishedTasks === 1 ? 'task' : 'tasks'}`, detail: s.finishedRoutines ? `and ${s.finishedRoutines} routines` : '' },
              { label: 'Typical session', value: s.sessions ? duration(s.averageSessionMinutes) : '–', detail: s.sessions ? `longest ${duration(s.longestSessionMinutes)}` : '' },
            ].map((t) => (
              <div key={t.label} className="rounded-xl border border-line bg-surface p-4 shadow-card">
                <div className="text-[12.5px] font-medium text-muted">{t.label}</div>
                <div className="mt-1 text-[24px] font-semibold tracking-tight text-fg tabular-nums">{t.value}</div>
                {t.detail && <div className="text-[12.5px] text-subtle">{t.detail}</div>}
              </div>
            ))}
          </div>

          <Section id="bars" title={s.weekly ? 'Week by week' : 'Day by day'} description="Bars are time worked; the numbers above them are things finished.">
            <div className="p-4">
              <BarChart
                values={s.bars.map((b) => b.workedMinutes)}
                labels={s.bars.map((b) => b.label)}
                topLabels={s.bars.map((b) => (b.finished > 0 && s.bars.length <= 31 ? String(b.finished) : ''))}
                tooltips={s.bars.map((b) => `${b.label}: ${duration(b.workedMinutes)} worked, ${b.finished} finished`)}
                everyOtherLabel={s.bars.length > 14}
              />
            </div>
          </Section>

          <Section
            id="hours"
            title="Your hours"
            description={bestHour >= 0 ? `You get the most done between ${hourText(bestHour)} and ${hourText((bestHour + 2) % 24)}` : 'Bars are time worked in each hour; dots are your average energy then.'}
          >
            <div className="p-4">
              <BarChart
                values={hourRange.map((h) => h.minutes)}
                labels={hourRange.map((h) => hourAxis.format(new Date(2000, 0, 1, h.hour)))}
                dots={hourRange.map((h) => h.energy)}
                tooltips={hourRange.map((h) => `${hourText(h.hour)}: ${duration(h.minutes)} worked${h.energy != null ? `\nAverage energy ${h.energy.toFixed(1)} of 10` : ''}`)}
              />
            </div>
          </Section>

          <Section id="estimates" title="Estimates">
            <div className="px-4 py-3">
              <div className="text-[14px] font-medium text-fg">
                {e.tasks < 3
                  ? 'Not enough to go on yet'
                  : e.ratio > 1.15
                    ? `Things take about ${ratio(e.ratio)} as long as you expect`
                    : e.ratio < 0.85
                      ? `Things take less time than you expect (about ${ratio(e.ratio)})`
                      : 'Your estimates are about right'}
              </div>
              <div className="mt-0.5 text-[12.5px] text-muted">
                {e.tasks < 3
                  ? 'Time a few tasks through to done and this shows how long things really take compared with your estimates.'
                  : `Typical result over ${e.tasks} finished tasks with time logged.${
                      e.ratio <= 1.15 ? '' : s.paceActive ? ' New blocks already allow for this.' : !s.planWithPace ? ' Turn on “Plan with your real pace” in Settings and new blocks will allow for this.' : ''
                    }`}
              </div>
            </div>
            {e.tasks >= 3 &&
              [...e.bySize.filter((c) => c.tasks >= 2).map((c) => ({ key: `s${c.size}`, name: sizeText[c.size] ?? c.size, ...c })), ...e.byCategory.filter((c) => c.tasks >= 2).map((c) => ({ key: `c${c.name}`, ...c }))].map((c) => (
                <div key={c.key} className="flex items-center px-4 py-2.5">
                  <div className="flex-1">
                    <div className="text-[13.5px] text-fg">{c.name}</div>
                    <div className="text-[12px] text-subtle">
                      {c.tasks} {c.tasks === 1 ? 'task' : 'tasks'}
                    </div>
                  </div>
                  <span className={clsx('text-[14px] font-semibold tabular-nums', ratioTone(c.ratio))}>{ratio(c.ratio)}</span>
                </div>
              ))}
          </Section>

          {s.moving.length > 0 && (
            <Section id="moving" title="Keeps getting moved" description="Bumped more than once lately. A smaller first step might help, or it might be fine to let it go.">
              {s.moving.map((m) => (
                <button key={m.id} onClick={() => nav.openTask(m.id)} className="flex w-full items-center px-4 py-2.5 text-left hover:bg-hover">
                  <span className="flex-1 text-[13.5px] text-fg">{m.title}</span>
                  <span className="mr-2 text-[12.5px] text-subtle">moved {m.bumps}×</span>
                  <ArrowUpRight className="size-3.5 text-subtle" />
                </button>
              ))}
            </Section>
          )}

          {s.tasks.length > 0 && (
            <Section id="where" title="Where the time went">
              {s.tasks.slice(0, allTasks ? undefined : 10).map((t) => (
                <button key={t.id} onClick={() => nav.openTask(t.id)} className="flex w-full items-center gap-3 px-4 py-2.5 text-left hover:bg-hover">
                  <span className="min-w-0 flex-1">
                    <span className="block truncate text-[13.5px] text-fg">{t.title}</span>
                    <span className="block text-[12px] text-subtle">
                      {t.finished ? 'Done · ' : ''}estimate {duration(t.estimate)} · {t.sessions} {t.sessions === 1 ? 'session' : 'sessions'}
                    </span>
                  </span>
                  <span className="text-[13.5px] font-medium text-fg tabular-nums">{duration(t.worked)}</span>
                </button>
              ))}
              {s.tasks.length > 10 && (
                <button onClick={() => setAllTasks(!allTasks)} className="w-full px-4 py-2.5 text-left text-[13px] font-medium text-accent-text hover:bg-hover">
                  {allTasks ? 'Show fewer' : `Show all ${s.tasks.length}`}
                </button>
              )}
            </Section>
          )}
        </>
      )}
    </div>
  )
}
