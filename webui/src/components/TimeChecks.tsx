import { Timer, X } from 'lucide-react'
import { useCallback, useState } from 'react'
import { call, useSignal } from '../bridge/hooks'
import type { TimeCheck } from '../bridge/types'
import { duration, time } from '../lib/format'
import { Dialog, TextField } from './Dialog'
import { Button, IconButton } from './ui'

function when(c: TimeCheck): string {
  return `Due ${time(c.start)} for ${duration(c.planned)} · ticked off ${time(c.doneAt)}`
}

// The answers to "how long did it take?": as planned, all the time since it
// was due, or a number of minutes.
function Choices({ check, onDone }: { check: TimeCheck; onDone?: () => void }) {
  const [other, setOther] = useState<string | null>(null)
  const log = (minutes: number) => {
    call('app', 'resolveTimeCheck', check.id, minutes)
    onDone?.()
  }
  if (other !== null) {
    const minutes = Number(other)
    return (
      <div className="flex items-center gap-2">
        <div className="w-20">
          <TextField
            autoFocus
            value={other}
            aria-label="Minutes"
            onChange={(e) => setOther(e.target.value.replace(/[^0-9]/g, ''))}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && minutes > 0) log(minutes)
              if (e.key === 'Escape') {
                e.stopPropagation()
                setOther(null)
              }
            }}
          />
        </div>
        <span className="text-[13px] text-subtle">min</span>
        <Button size="sm" variant="primary" disabled={!(minutes > 0)} onClick={() => log(minutes)}>
          Log
        </Button>
        <Button size="sm" variant="ghost" onClick={() => setOther(null)}>
          Back
        </Button>
      </div>
    )
  }
  return (
    <div className="flex flex-wrap items-center gap-2">
      <Button size="sm" variant="secondary" onClick={() => log(check.planned)}>
        {duration(check.planned)}, as planned
      </Button>
      <Button size="sm" variant="secondary" onClick={() => log(check.elapsed)}>
        {duration(check.elapsed)}, since {time(check.start)}
      </Button>
      <Button size="sm" variant="secondary" onClick={() => setOther('')}>
        Other…
      </Button>
      <Button size="sm" variant="ghost" onClick={() => log(0)}>
        Don't log it
      </Button>
    </div>
  )
}

// Asks straight away when something is ticked off long after it was due and
// the timer wasn't running. Closing it leaves the question on Today.
export function TimeCheckDialog() {
  const [check, setCheck] = useState<TimeCheck | null>(null)
  useSignal(
    'app',
    'timeCheckNeeded',
    useCallback((c: TimeCheck) => setCheck(c), []),
  )
  return (
    <Dialog
      open={!!check}
      onOpenChange={(o) => !o && setCheck(null)}
      width="sm"
      title={check ? `How long did “${check.title}” take?` : ''}
      description={check ? `${when(check)} · the timer wasn't running` : ''}
      footer={
        <Button variant="ghost" onClick={() => setCheck(null)}>
          Ask me later
        </Button>
      }
    >
      {check && <Choices key={check.id} check={check} onDone={() => setCheck(null)} />}
    </Dialog>
  )
}

// Today: the questions not answered yet.
export function TimeChecks({ checks }: { checks: TimeCheck[] }) {
  if (!checks.length) return null
  return (
    <div className="animate-pop-in rounded-xl border border-accent/25 bg-accent-soft px-4 py-3">
      <div className="flex items-center gap-3">
        <Timer className="size-5 shrink-0 text-accent-text" />
        <div className="flex-1 text-[13.5px] font-medium text-fg">How long did {checks.length === 1 ? 'it' : 'these'} take?</div>
      </div>
      <ul className="mt-2 flex flex-col gap-2 pl-8">
        {checks.map((c) => (
          <li key={c.id} className="group flex items-start gap-2">
            <div className="min-w-0 flex-1">
              <div className="text-[13.5px]">
                <span className="font-medium text-fg">{c.title}</span> <span className="text-muted">{when(c)}</span>
              </div>
              <div className="mt-1.5">
                <Choices check={c} />
              </div>
            </div>
            <IconButton label="Don't log it" size="sm" className="opacity-60 group-hover:opacity-100" onClick={() => call('app', 'resolveTimeCheck', c.id, 0)}>
              <X className="size-3.5" />
            </IconButton>
          </li>
        ))}
      </ul>
    </div>
  )
}
