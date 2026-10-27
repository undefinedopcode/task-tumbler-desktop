import { Check, Pause, Target } from 'lucide-react'
import { useEffect, useState } from 'react'
import { call, useProp } from '../bridge/hooks'
import type { Working } from '../bridge/types'
import { elapsed } from '../lib/format'
import { Button } from './ui'

// What's being worked on, with a running clock. Hidden when nothing is.
export function WorkingCard() {
  const working = useProp<Working>('app', 'working', {})
  const [now, setNow] = useState(Date.now())
  useEffect(() => {
    if (!working.startedAt) return
    const t = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(t)
  }, [working.startedAt])
  if (!working.taskId || !working.startedAt) return null

  return (
    <div className="relative overflow-hidden rounded-xl border border-accent/30 bg-accent-soft p-4">
      <div className="pointer-events-none absolute -top-16 -right-10 size-48 rounded-full bg-accent/10 blur-2xl" />
      <div className="relative flex items-center gap-4">
        <div className="relative flex size-11 shrink-0 items-center justify-center rounded-full bg-accent text-accent-fg shadow-card">
          <span className="absolute inset-0 animate-ping rounded-full bg-accent opacity-20 [animation-duration:2.4s]" />
          <Target className="size-5" />
        </div>
        <div className="min-w-0 flex-1">
          <div className="text-[12px] font-semibold tracking-wide text-accent-text uppercase">Working on</div>
          <div className="truncate text-[16px] font-semibold text-fg">{working.title}</div>
        </div>
        <div className="font-mono text-[22px] font-semibold text-fg tabular-nums">{elapsed(working.startedAt, now)}</div>
        <div className="flex gap-2">
          <Button variant="secondary" icon={<Pause className="size-4" />} onClick={() => call('app', 'stopWork')}>
            Stop
          </Button>
          <Button variant="primary" icon={<Check className="size-4" />} onClick={() => call('app', 'setTaskStatus', working.taskId, 'completed')}>
            Done
          </Button>
        </div>
      </div>
    </div>
  )
}
