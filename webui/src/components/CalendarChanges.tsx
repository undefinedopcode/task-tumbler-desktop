import clsx from 'clsx'
import { CalendarClock, X } from 'lucide-react'
import { call } from '../bridge/hooks'
import type { CalendarChange } from '../bridge/types'
import { time } from '../lib/format'
import { Button, Chip, IconButton } from './ui'

const kindLabel: Record<CalendarChange['kind'], string> = { added: 'New', moved: 'Moved', cancelled: 'Cancelled', changed: 'New place' }

// "just now", "12 min ago", "3 h ago", "yesterday 18:20", "Mon 27 Oct 18:20".
function ago(ms: number): string {
  const min = Math.floor((Date.now() - ms) / 60000)
  if (min < 1) return 'just now'
  if (min < 60) return `${min} min ago`
  if (new Date(ms).toDateString() === new Date().toDateString()) return `${Math.floor(min / 60)} h ago`
  const yesterday = new Date()
  yesterday.setDate(yesterday.getDate() - 1)
  if (new Date(ms).toDateString() === yesterday.toDateString()) return `yesterday ${time(ms)}`
  return `${new Date(ms).toLocaleDateString(undefined, { weekday: 'short', day: 'numeric', month: 'short' })} ${time(ms)}`
}

function detail(c: CalendarChange): string {
  switch (c.kind) {
    case 'added':
      return c.location ? `${c.when}, ${c.location}` : c.when
    case 'moved':
      return `Now ${c.when}, was ${c.oldWhen}`
    case 'cancelled':
      return `Was ${c.when}; that time is free now`
    case 'changed':
      return c.oldLocation ? `${c.when}, now in ${c.location || 'no set place'} (was ${c.oldLocation})` : `${c.when}, now in ${c.location || 'no set place'}`
  }
}

// Meetings someone else added, moved or cancelled since you last looked, so
// a short-notice invite doesn't slip past. Stays until dismissed.
export function CalendarChanges({ changes }: { changes: CalendarChange[] }) {
  if (!changes.length) return null
  const pressing = changes.some((c) => c.urgency !== 'later' && c.kind !== 'cancelled')
  return (
    <div className={clsx('animate-pop-in rounded-xl border px-4 py-3', pressing ? 'border-warning/30 bg-warning-soft' : 'border-accent/25 bg-accent-soft')}>
      <div className="flex items-center gap-3">
        <CalendarClock className={clsx('size-5 shrink-0', pressing ? 'text-warning' : 'text-accent-text')} />
        <div className="flex-1 text-[13.5px] font-medium text-fg">{changes.length === 1 ? 'A calendar change' : `${changes.length} calendar changes`}</div>
        <Button size="sm" variant="ghost" onClick={() => call('app', 'dismissCalendarChanges')}>
          Got it
        </Button>
      </div>
      <ul className="mt-2 flex flex-col gap-1 pl-8">
        {changes.map((c) => (
          <li key={c.key} className="group flex items-start gap-2 rounded-lg py-1">
            <div className="min-w-0 flex-1">
              <div className="flex flex-wrap items-center gap-x-2 gap-y-1 text-[13.5px]">
                <Chip tone={c.kind === 'cancelled' ? 'neutral' : c.urgency === 'soon' ? 'danger' : c.urgency === 'today' ? 'warning' : 'accent'}>
                  {kindLabel[c.kind]}
                </Chip>
                <span className={clsx('font-medium text-fg', c.kind === 'cancelled' && 'line-through decoration-line-strong')}>{c.title}</span>
                <span className="text-muted">{detail(c)}</span>
                {c.underway && <span className="text-[12.5px] font-medium text-danger">under way</span>}
              </div>
              {c.clashes.map((b) => (
                <div key={b.blockId} className="mt-1 flex flex-wrap items-center gap-2 text-[12.5px] text-muted">
                  <span>
                    Runs into “{b.title}” ({b.when})
                  </span>
                  <Button size="sm" variant="secondary" onClick={() => call('app', 'moveAfterEvent', b.blockId, c.key)}>
                    Move it after
                  </Button>
                </div>
              ))}
              <div className="mt-0.5 text-[12px] text-subtle">Changed {ago(c.seenAt)}</div>
            </div>
            <IconButton label="Dismiss" size="sm" className="opacity-60 group-hover:opacity-100" onClick={() => call('app', 'dismissCalendarChange', c.key)}>
              <X className="size-3.5" />
            </IconButton>
          </li>
        ))}
      </ul>
    </div>
  )
}
