import clsx from 'clsx'
import { Check, Clock, Flag, Play, Zap } from 'lucide-react'
import { useState } from 'react'
import { call } from '../bridge/hooks'
import type { Energy, ReadyTask } from '../bridge/types'
import { dueLabel, duration } from '../lib/format'
import { useNav } from '../lib/nav'
import { ScheduleMenu } from './ScheduleDialog'
import { Button, Chip } from './ui'

export const energyLabel: Record<Energy, string> = { low: 'Low', medium: 'Medium', high: 'High', peak: 'Peak' }

// A task that can be started now: tick it off, start it, or give it a slot.
export function ReadyRow({ task }: { task: ReadyTask }) {
  const [leaving, setLeaving] = useState(false)
  const nav = useNav()
  const due = dueLabel(task.dueDate)
  const finish = () => {
    setLeaving(true)
    setTimeout(() => call('app', 'setTaskStatus', task.id, 'completed'), 180)
  }
  return (
    <div
      className={clsx(
        'group flex items-center gap-3 rounded-lg px-3 py-2.5 transition-all hover:bg-hover',
        leaving && 'translate-x-2 opacity-0',
      )}
    >
      <button
        onClick={finish}
        aria-label={`Mark “${task.title}” done`}
        className="flex size-[18px] shrink-0 items-center justify-center rounded-full border-[1.5px] border-line-strong text-transparent transition-colors hover:border-success hover:bg-success-soft hover:text-success"
      >
        <Check className="size-3" strokeWidth={3} />
      </button>
      <div className="min-w-0 flex-1">
        <button onClick={() => nav.openTask(task.id)} className="block max-w-full truncate text-left text-[14px] text-fg hover:underline hover:decoration-line-strong hover:underline-offset-2">
          {task.title}
        </button>
        <div className="mt-0.5 flex flex-wrap items-center gap-1.5">
          {(task.priority === 'urgent' || task.priority === 'high') && (
            <Chip tone="danger" icon={<Flag className="size-3" />}>
              {task.priority === 'urgent' ? 'Urgent' : 'High'}
            </Chip>
          )}
          {due && <Chip tone={due.tone === 'muted' ? 'neutral' : due.tone}>{due.text}</Chip>}
          {task.estimate > 0 && (
            <Chip icon={<Clock className="size-3" />}>{duration(task.estimate)}</Chip>
          )}
          <Chip icon={<Zap className="size-3" />}>{energyLabel[task.energy]}</Chip>
          {task.subtasksTotal > 0 && (
            <Chip>
              {task.subtasksDone}/{task.subtasksTotal} steps
            </Chip>
          )}
          {task.tags.slice(0, 3).map((t) => (
            <span key={t} className="text-[12px] text-subtle">
              #{t}
            </span>
          ))}
        </div>
      </div>
      <div className="flex items-center gap-1 opacity-0 transition-opacity group-focus-within:opacity-100 group-hover:opacity-100">
        <ScheduleMenu taskId={task.id} title={task.title} estimate={task.estimate} />
        <Button size="sm" variant="soft" icon={<Play className="size-3.5" />} onClick={() => call('app', 'startWork', task.id)}>
          Start
        </Button>
      </div>
    </div>
  )
}
