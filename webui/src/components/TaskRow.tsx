import clsx from 'clsx'
import { CalendarClock, Check, Clock, Flag, Lock, Play, Repeat, RotateCcw } from 'lucide-react'
import { useState } from 'react'
import { call } from '../bridge/hooks'
import type { TaskRowData } from '../bridge/types'
import { dueLabel, duration, time } from '../lib/format'
import { useNav } from '../lib/nav'
import { ScheduleMenu } from './ScheduleDialog'
import { Button, Chip, Dot, IconButton } from './ui'

function nextBlockLabel(ms: number): string {
  const d = new Date(ms)
  const today = new Date()
  const tomorrow = new Date()
  tomorrow.setDate(today.getDate() + 1)
  if (d.toDateString() === today.toDateString()) return time(ms)
  if (d.toDateString() === tomorrow.toDateString()) return `Tomorrow ${time(ms)}`
  return d.toLocaleDateString(undefined, { weekday: 'short', day: 'numeric', month: 'short' })
}

// One task in the Tasks list. Click to open it; tick, start or schedule on hover.
export function TaskRow({ task, selected }: { task: TaskRowData; selected?: boolean }) {
  const nav = useNav()
  const [leaving, setLeaving] = useState(false)
  const finished = task.status === 'completed' || task.status === 'cancelled'
  const due = finished ? null : dueLabel(task.dueDate)
  const toggle = () => {
    if (finished) return call('app', 'setTaskStatus', task.taskId, 'pending')
    setLeaving(true)
    setTimeout(() => call('app', 'setTaskStatus', task.taskId, 'completed'), 180)
  }

  return (
    <div
      onClick={() => nav.openTask(task.taskId)}
      className={clsx(
        'group flex cursor-default items-center gap-3 rounded-lg px-3 py-2.5 transition-all',
        selected ? 'bg-accent-soft' : 'hover:bg-hover',
        leaving && 'translate-x-2 opacity-0',
      )}
    >
      <button
        onClick={(e) => {
          e.stopPropagation()
          toggle()
        }}
        aria-label={finished ? `Reopen “${task.title}”` : `Mark “${task.title}” done`}
        className={clsx(
          'flex size-[18px] shrink-0 items-center justify-center rounded-full border-[1.5px] transition-colors',
          finished
            ? 'border-success bg-success text-white'
            : 'border-line-strong text-transparent hover:border-success hover:bg-success-soft hover:text-success',
        )}
      >
        <Check className="size-3" strokeWidth={3} />
      </button>
      <div className="min-w-0 flex-1">
        <div className="flex items-center gap-2">
          {task.categoryColor && <Dot color={task.categoryColor} />}
          <span className={clsx('truncate text-[14px]', finished ? 'text-subtle line-through' : 'text-fg')}>{task.title}</span>
          {task.status === 'in_progress' && <Chip tone="accent">In progress</Chip>}
        </div>
        <div className="mt-0.5 flex flex-wrap items-center gap-1.5">
          {task.parentTitle && <span className="truncate text-[12px] text-subtle">in {task.parentTitle}</span>}
          {task.blocked && (
            <Chip tone="warning" icon={<Lock className="size-3" />}>
              Waiting
            </Chip>
          )}
          {!finished && (task.priority === 'urgent' || task.priority === 'high') && (
            <Chip tone="danger" icon={<Flag className="size-3" />}>
              {task.priority === 'urgent' ? 'Urgent' : 'High'}
            </Chip>
          )}
          {due && <Chip tone={due.tone === 'muted' ? 'neutral' : due.tone}>{due.text}</Chip>}
          {task.nextBlock > 0 && !finished && (
            <Chip tone="accent" icon={<CalendarClock className="size-3" />}>
              {nextBlockLabel(task.nextBlock)}
            </Chip>
          )}
          {task.isRecurring && (
            <Chip icon={<Repeat className="size-3" />}>{task.recurrenceText || 'Repeats'}</Chip>
          )}
          {task.estimate > 0 && <Chip icon={<Clock className="size-3" />}>{duration(task.estimate)}</Chip>}
          {task.subtasksTotal > 0 && (
            <Chip>
              {task.subtasksDone}/{task.subtasksTotal} steps
            </Chip>
          )}
          {task.loggedMinutes > 0 && <span className="text-[12px] text-subtle">{duration(task.loggedMinutes)} logged</span>}
          {task.tags.slice(0, 3).map((t) => (
            <span key={t} className="text-[12px] text-subtle">
              #{t}
            </span>
          ))}
        </div>
      </div>
      <div className="flex items-center gap-1 opacity-0 transition-opacity group-focus-within:opacity-100 group-hover:opacity-100" onClick={(e) => e.stopPropagation()}>
        {finished ? (
          <IconButton label="Reopen" size="sm" onClick={() => call('app', 'setTaskStatus', task.taskId, 'pending')}>
            <RotateCcw className="size-4" />
          </IconButton>
        ) : (
          <>
            {!task.isRecurring && (
              <ScheduleMenu taskId={task.taskId} title={task.title} estimate={task.estimate} />
            )}
            {!task.blocked && (
              <Button size="sm" variant="soft" icon={<Play className="size-3.5" />} onClick={() => call('app', 'startWork', task.taskId)}>
                Start
              </Button>
            )}
          </>
        )}
      </div>
    </div>
  )
}
