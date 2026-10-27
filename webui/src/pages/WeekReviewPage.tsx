import { CalendarPlus, Check, CheckCircle2, Lightbulb, Loader2, RefreshCw, Sparkles, StickyNote, X } from 'lucide-react'
import { useCallback, useState } from 'react'
import { call, useProp, useQuery, useSignal } from '../bridge/hooks'
import type { AiState } from '../bridge/types'
import { BarChart } from '../components/BarChart'
import { Section } from '../components/settings'
import { Button, Tip } from '../components/ui'
import { dueLabel, duration } from '../lib/format'
import { useNav } from '../lib/nav'
import { keys, usePlatform } from '../lib/platform'

interface Week {
  rangeLabel: string
  workedMinutes: number
  sessions: number
  finishedTasks: number
  finishedRoutines: number
  days: { label: string; workedMinutes: number; finished: number }[]
  wins: { taskId: string; title: string; at: number; routine: boolean }[]
  moving: { taskId: string; title: string; bumps: number }[]
  stale: { taskId: string; title: string; days: number }[]
  dueSoon: { taskId: string; title: string; due: string; overdue: boolean }[]
  dayNotes: { id: string; title: string; content: string }[]
  doneToday: boolean
}

// The weekly review: what the week held, then the few things worth a decision,
// each with a one-tap answer. An AI reading of the week if wanted.
export function WeekReviewPage() {
  const nav = useNav()
  const platform = usePlatform()
  const ai = useProp<AiState>('app', 'ai', {})
  const week = useQuery<Week | null>('app', 'weekReview', [], null)
  const [summary, setSummary] = useState<{ summary: string; suggestions: string[]; by: string } | null>(null)
  const [asking, setAsking] = useState(false)
  const [error, setError] = useState('')
  const [allWins, setAllWins] = useState(false)
  useSignal(
    'app',
    'weekSummaryReady',
    useCallback((r: { summary: string; suggestions: string[]; by: string }) => {
      setAsking(false)
      setError('')
      setSummary(r)
    }, []),
  )
  useSignal(
    'app',
    'aiFailed',
    useCallback((feature: string, e: string) => {
      if (feature !== 'week') return
      setAsking(false)
      setError(e)
    }, []),
  )
  if (!week) return null
  const quiet = week.workedMinutes === 0 && week.wins.length === 0
  const ask = () => {
    setAsking(true)
    setError('')
    setSummary(null)
    call('app', 'summarizeWeek')
  }

  const decision = (taskId: string, title: string, detail: string, canLetGo = true) => (
    <div key={taskId} className="flex items-center gap-3 px-4 py-2.5">
      <button onClick={() => nav.openTask(taskId)} className="min-w-0 flex-1 text-left">
        <div className="truncate text-[13.5px] text-fg">{title}</div>
        <div className="text-[12px] text-subtle">{detail}</div>
      </button>
      <Button size="sm" variant="ghost" icon={<CalendarPlus className="size-3.5" />} onClick={() => call('app', 'scheduleNextSlot', taskId)}>
        Next free slot
      </Button>
      {canLetGo && (
        <Tip label={`Cancel the task (${keys(platform, 'Mod+Z')} to undo)`}>
          <Button size="sm" variant="ghost" icon={<X className="size-3.5" />} onClick={() => call('app', 'letGo', taskId)}>
            Let it go
          </Button>
        </Tip>
      )}
    </div>
  )

  return (
    <div className="mx-auto flex w-full max-w-[800px] flex-col gap-8 px-8 pt-8 pb-16">
      <header>
        <div className="text-[13px] font-medium text-muted">{week.rangeLabel}</div>
        <h1 className="mt-0.5 text-[26px] leading-tight font-semibold tracking-tight">Week review</h1>
      </header>

      <div className="rounded-2xl border border-line bg-surface p-5 shadow-card">
        <div className="grid grid-cols-3 gap-4">
          {[
            { label: 'Time worked', value: duration(week.workedMinutes), detail: `in ${week.sessions} ${week.sessions === 1 ? 'session' : 'sessions'}` },
            { label: 'Tasks finished', value: String(week.finishedTasks) },
            { label: 'Routines done', value: String(week.finishedRoutines) },
          ].map((t) => (
            <div key={t.label}>
              <div className="text-[12.5px] font-medium text-muted">{t.label}</div>
              <div className="text-[24px] font-semibold tracking-tight text-fg tabular-nums">{t.value}</div>
              {t.detail && <div className="text-[12px] text-subtle">{t.detail}</div>}
            </div>
          ))}
        </div>
        <div className="mt-4">
          {quiet ? (
            <p className="text-[13px] text-muted">A quiet week, at least as far as the timer knows. That's allowed.</p>
          ) : (
            <BarChart
              height={110}
              values={week.days.map((d) => d.workedMinutes)}
              labels={week.days.map((d) => d.label)}
              topLabels={week.days.map((d) => (d.finished ? String(d.finished) : ''))}
              tooltips={week.days.map((d) => `${d.label}: ${duration(d.workedMinutes)} worked, ${d.finished} finished`)}
            />
          )}
        </div>
      </div>

      {ai.provider && ai.provider !== 'off' && (
        <div className="rounded-2xl border border-accent/25 bg-accent-soft/60 p-5">
          {!summary ? (
            <div className="flex items-center gap-4">
              <Sparkles className="size-5 shrink-0 text-accent" />
              <div className="flex-1 text-[13px] text-muted">
                {error ? `That didn't work: ${error}` : 'A short, kind look back and up to three things to try. Only task titles are shared unless you allow notes.'}
              </div>
              <Button variant="primary" disabled={!ai.ready || asking} icon={asking ? <Loader2 className="size-4 animate-spin" /> : undefined} onClick={ask}>
                {asking ? 'Thinking it over…' : ai.ready ? `Ask ${ai.label}` : 'AI isn’t set up yet'}
              </Button>
            </div>
          ) : (
            <div>
              <p className="text-[14px] leading-relaxed text-fg select-text">{summary.summary}</p>
              <ul className="mt-3 flex flex-col gap-2">
                {summary.suggestions.map((s, i) => (
                  <li key={i} className="flex gap-2 text-[13.5px] text-fg">
                    <Lightbulb className="mt-0.5 size-4 shrink-0 text-warning" />
                    {s}
                  </li>
                ))}
              </ul>
              <div className="mt-3 flex items-center gap-2 text-[12px] text-subtle">
                From {summary.by}
                <Button size="sm" variant="ghost" className="ml-auto" disabled={asking} icon={<RefreshCw className="size-3.5" />} onClick={ask}>
                  Ask again
                </Button>
              </div>
            </div>
          )}
        </div>
      )}

      {week.wins.length > 0 && (
        <Section id="wins" title="Done this week">
          {week.wins.slice(0, allWins ? undefined : 8).map((w) => (
            <button key={`${w.taskId}${w.at}`} onClick={() => nav.openTask(w.taskId)} className="flex w-full items-center gap-2.5 px-4 py-2 text-left hover:bg-hover">
              <CheckCircle2 className="size-4 text-success" />
              <span className="flex-1 text-[13.5px] text-fg">{w.title}</span>
              <span className="text-[12px] text-subtle">
                {new Date(w.at).toLocaleDateString(undefined, { weekday: 'long' })}
                {w.routine ? ' · routine' : ''}
              </span>
            </button>
          ))}
          {week.wins.length > 8 && (
            <button onClick={() => setAllWins(!allWins)} className="w-full px-4 py-2.5 text-left text-[13px] font-medium text-accent-text hover:bg-hover">
              {allWins ? 'Show fewer' : `Show all ${week.wins.length}`}
            </button>
          )}
        </Section>
      )}

      {week.moving.length > 0 && (
        <Section id="moving" title="Keeps getting moved" description="Moved more than once this week. Often the first step is too big: open it and break it down, give it a slot, or let it go.">
          {week.moving.map((m) => decision(m.taskId, m.title, `Moved ${m.bumps} times`))}
        </Section>
      )}
      {week.dueSoon.length > 0 && (
        <Section id="due" title="Due soon, not scheduled">
          {week.dueSoon.map((d) => decision(d.taskId, d.title, dueLabel(d.due)?.text ?? d.due, false))}
        </Section>
      )}
      {week.stale.length > 0 && (
        <Section id="stale" title="Gone quiet" description="Untouched for a few weeks. Still wanted? Letting go of a few keeps the list honest.">
          {week.stale.map((s) => decision(s.taskId, s.title, `Untouched for ${s.days} days`))}
        </Section>
      )}
      {week.dayNotes.length > 0 && (
        <Section id="notes" title="Your day notes">
          {week.dayNotes.map((n) => (
            <button key={n.id} onClick={() => nav.openNote(n.id)} className="flex w-full items-start gap-2.5 px-4 py-2.5 text-left hover:bg-hover">
              <StickyNote className="mt-0.5 size-4 text-subtle" />
              <span className="min-w-0 flex-1">
                <span className="block text-[13.5px] text-fg">{n.title}</span>
                <span className="line-clamp-2 block text-[12.5px] text-muted">{n.content}</span>
              </span>
            </button>
          ))}
        </Section>
      )}

      <div className="flex items-center justify-end gap-3">
        <span className="text-[12.5px] text-muted">Next up: plan tomorrow, or just enjoy the rest of the day.</span>
        <Button
          variant="primary"
          icon={<Check className="size-4" />}
          onClick={() => {
            call('app', 'finishReview', false)
            nav.showPage('today')
          }}
        >
          {week.doneToday ? 'Week reviewed' : 'Done reviewing'}
        </Button>
      </div>
    </div>
  )
}
