import clsx from 'clsx'
import {
  ArrowUpRight,
  CalendarPlus,
  Check,
  ChevronRight,
  Clock,
  ExternalLink,
  FileText,
  Globe,
  Maximize2,
  MoreHorizontal,
  Pause,
  Play,
  Plus,
  RotateCcw,
  Sparkles,
  SquareTerminal,
  StickyNote,
  Trash2,
  X,
  AppWindow,
} from 'lucide-react'
import { useCallback, useEffect, useState } from 'react'
import { call, useProp, useQuery, useSignal } from '../bridge/hooks'
import type { AiState, Category, NoteSummary, ProposedStep, Recurrence, ScheduleBlock, Task, TaskAction, TaskDetail, Working } from '../bridge/types'
import { duration } from '../lib/format'
import { useNav } from '../lib/nav'
import { EditableText, Field, PanelSection } from './fields'
import { Menu, MenuContent, MenuItem, MenuTrigger } from './Menu'
import { Segmented } from './Segmented'
import { DatePicker } from './DatePicker'
import { Select } from './Select'
import { TimePicker } from './TimePicker'
import { LauncherDialog } from './LauncherDialog'
import { ScheduleDialog, ScheduleMenu } from './ScheduleDialog'
import { Button, Checkbox, Chip, Dot, IconButton, Toggle } from './ui'

const statusLabel: Record<string, string> = { pending: 'To do', in_progress: 'In progress', completed: 'Done', cancelled: 'Cancelled' }
const estimates = [5, 15, 30, 45, 60, 90]
// Monday first; values are JS/Qt weekday numbers (0 = Sunday).
const weekdays = [
  { day: 1, label: 'Mon' },
  { day: 2, label: 'Tue' },
  { day: 3, label: 'Wed' },
  { day: 4, label: 'Thu' },
  { day: 5, label: 'Fri' },
  { day: 6, label: 'Sat' },
  { day: 0, label: 'Sun' },
]

// Everything about one task, beside whatever page you're on. Each change
// saves straight away as its own undoable step.
export function TaskDetailPanel({ taskId, onClose }: { taskId: string; onClose: () => void }) {
  const detail = useQuery<TaskDetail | null>('app', 'taskDetail', [taskId], null)
  const working = useProp<Working>('app', 'working', {})
  const categories = useProp<Category[]>('app', 'categories', [])
  const ai = useProp<AiState>('app', 'ai', {})
  const nav = useNav()
  const [launcherOpen, setLauncherOpen] = useState(false)
  // The schedule dialog: closed, a new block, or moving one.
  const [scheduling, setScheduling] = useState<null | { block?: ScheduleBlock }>(null)

  // Gone (deleted, or undone into nonexistence): close.
  useEffect(() => {
    if (detail && !detail.id) onClose()
  }, [detail, onClose])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !(e.target instanceof HTMLTextAreaElement) && !(e.target instanceof HTMLInputElement)) onClose()
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [onClose])

  if (!detail || !detail.id) return null
  const update = (fields: Record<string, unknown>) => call('app', 'updateTask', taskId, fields)
  const finished = detail.status === 'completed' || detail.status === 'cancelled'
  const isWorking = working.taskId === taskId

  return (
    <aside className="flex h-full w-[520px] max-w-[60vw] shrink-0 animate-[slide-in-right_160ms_ease-out] flex-col border-l border-line bg-surface shadow-pop">
      {/* Header */}
      <div className="flex items-center gap-1 px-4 pt-3 pb-2">
        <IconButton label="Close" size="sm" onClick={onClose}>
          <X className="size-4" />
        </IconButton>
        {detail.parentTitle && (
          <button
            onClick={() => nav.openTask(detail.parentId)}
            className="ml-1 flex min-w-0 items-center gap-1 rounded-md px-1.5 py-0.5 text-[12.5px] text-muted hover:bg-hover hover:text-fg"
          >
            <span className="truncate">{detail.parentTitle}</span>
            <ChevronRight className="size-3 shrink-0" />
          </button>
        )}
        <div className="ml-auto flex items-center gap-1">
          {!finished && (
            <>
              {!detail.isRecurring && (
                <ScheduleMenu taskId={taskId} title={detail.title} estimate={detail.estimate} />
              )}
              <IconButton label="Focus: start and hide everything else" size="sm" onClick={() => call('app', 'startFocus', taskId)}>
                <Maximize2 className="size-4" />
              </IconButton>
              <Button
                size="sm"
                variant="soft"
                icon={isWorking ? <Pause className="size-3.5" /> : <Play className="size-3.5" />}
                onClick={() => (isWorking ? call('app', 'stopWork') : call('app', 'startWork', taskId))}
              >
                {isWorking ? 'Pause' : 'Start'}
              </Button>
            </>
          )}
          <Button
            size="sm"
            variant={finished ? 'secondary' : 'primary'}
            icon={finished ? <RotateCcw className="size-3.5" /> : <Check className="size-3.5" />}
            onClick={() => call('app', 'setTaskStatus', taskId, finished ? 'pending' : 'completed')}
          >
            {finished ? 'Reopen' : 'Done'}
          </Button>
          <Menu>
            <MenuTrigger asChild>
              <IconButton label="More" size="sm">
                <MoreHorizontal className="size-4" />
              </IconButton>
            </MenuTrigger>
            <MenuContent>
              <MenuItem icon={<Trash2 />} danger onSelect={() => call('app', 'deleteTask', taskId)}>
                Delete task
              </MenuItem>
            </MenuContent>
          </Menu>
        </div>
      </div>

      <div className="min-h-0 flex-1 overflow-y-auto">
        {/* Title and properties */}
        <div className="px-4 pb-4">
          <EditableText
            aria-label="Title"
            value={detail.title}
            allowEmpty={false}
            onSave={(title) => update({ title })}
            className={clsx('px-2 py-1 text-[20px] leading-snug font-semibold tracking-tight', finished && 'text-muted line-through')}
          />
          <div className="mt-3 px-2">
            <Field label="Status">
              <Segmented
                size="sm"
                value={detail.status}
                onChange={(s) => call('app', 'setTaskStatus', taskId, s)}
                options={(['pending', 'in_progress', 'completed', 'cancelled'] as const).map((s) => ({ value: s, label: statusLabel[s] }))}
              />
            </Field>
            <Field label="Priority">
              <Segmented
                size="sm"
                value={detail.priority}
                onChange={(priority) => update({ priority })}
                options={[
                  { value: 'low', label: 'Low' },
                  { value: 'medium', label: 'Medium' },
                  { value: 'high', label: 'High' },
                  { value: 'urgent', label: 'Urgent' },
                ]}
              />
            </Field>
            <Field label="Energy">
              <Segmented
                size="sm"
                value={detail.energy}
                onChange={(e) => update({ energy_required: e })}
                options={[
                  { value: 'low', label: 'Low', title: 'Fine when tired' },
                  { value: 'medium', label: 'Medium' },
                  { value: 'high', label: 'High' },
                  { value: 'peak', label: 'Peak', title: 'Needs my best hours' },
                ]}
              />
            </Field>
            <Field label="Takes">
              <div className="flex flex-wrap items-center gap-1">
                {estimates.map((m) => (
                  <button
                    key={m}
                    onClick={() => update({ time_estimate_minutes: m })}
                    className={clsx(
                      'h-6 rounded-md px-2 text-[12.5px] font-medium tabular-nums transition-colors',
                      detail.estimate === m ? 'bg-accent text-accent-fg' : 'bg-hover text-muted hover:text-fg',
                    )}
                  >
                    {duration(m)}
                  </button>
                ))}
                {!estimates.includes(detail.estimate) && <Chip tone="accent">{duration(detail.estimate)}</Chip>}
                <MinutesInput value={detail.estimate} onSave={(m) => update({ time_estimate_minutes: m })} />
              </div>
            </Field>
            <Field label="Due">
              <DatePicker value={detail.dueDate} onChange={(due_date) => update({ due_date })} />
            </Field>
            <Field label="Category">
              <Select
                size="sm"
                value={detail.categoryId}
                onChange={(id) => update({ category_id: id })}
                placeholder="None"
                options={[{ value: '', label: 'None' }, ...categories.map((c) => ({ value: c.id, label: c.name, icon: <Dot color={c.color} /> }))]}
              />
            </Field>
            <Field label="Tags">
              <TagsInput tags={detail.tags} onSave={(tags) => update({ tags })} />
            </Field>
          </div>
          <div className="mt-2 px-0">
            <EditableText
              aria-label="Details"
              multiline
              value={detail.description}
              placeholder="Add details, links, the first step…"
              onSave={(description) => update({ description })}
              className="min-h-[60px] px-2 py-1.5 text-[13.5px] leading-relaxed text-fg"
            />
          </div>
        </div>

        <StepsSection detail={detail} ai={ai} />
        <RepeatSection detail={detail} update={update} />
        <WaitingSection detail={detail} />
        <NotesSection detail={detail} />
        <PanelSection title="When I start this">
          {detail.actions.map((a) => (
            <ActionRow key={a.id} action={a} />
          ))}
          <Button size="sm" variant="ghost" icon={<Plus className="size-3.5" />} className="-ml-1" onClick={() => setLauncherOpen(true)}>
            Add something to open
          </Button>
          {detail.actions.length === 0 && (
            <p className="mt-1 text-[12.5px] text-subtle">The doc, the project, a playlist: whatever makes starting easier.</p>
          )}
          <LauncherDialog taskId={taskId} open={launcherOpen} onOpenChange={setLauncherOpen} />
        </PanelSection>
        <PanelSection
          title="On the schedule"
          aside={detail.loggedMinutes > 0 ? `${duration(detail.loggedMinutes)} logged of ${duration(detail.estimate)}` : 'No time logged yet'}
        >
          {detail.schedules.length === 0 && <p className="text-[13px] text-subtle">Not scheduled.</p>}
          {detail.schedules.map((b) => (
            <div key={b.id} className="group flex h-8 items-center gap-2 rounded-lg px-1 hover:bg-hover">
              <Clock className="size-3.5 text-subtle" />
              {b.completed ? (
                <span className="flex-1 text-[13px] text-subtle line-through">{b.label}</span>
              ) : (
                <button className="flex-1 text-left text-[13px] hover:underline" title="Change day or time" onClick={() => setScheduling({ block: b })}>
                  {b.label}
                </button>
              )}
              <span className="text-[12px] text-subtle">{duration(Math.round((b.end - b.start) / 60000))}</span>
              {!b.completed && (
                <IconButton label="Remove from schedule" size="sm" className="opacity-0 group-hover:opacity-100" onClick={() => call('app', 'unschedule', b.id)}>
                  <X className="size-3.5" />
                </IconButton>
              )}
            </div>
          ))}
          {!finished && !detail.isRecurring && (
            <div className="mt-1 -ml-1 flex flex-wrap gap-1">
              <Button size="sm" variant="ghost" icon={<CalendarPlus className="size-3.5" />} onClick={() => call('app', 'scheduleNextSlot', taskId)}>
                Next free slot
              </Button>
              <Button size="sm" variant="ghost" icon={<Clock className="size-3.5" />} onClick={() => setScheduling({})}>
                Pick a day and time…
              </Button>
            </div>
          )}
          <ScheduleDialog
            taskId={taskId}
            title={detail.title}
            estimate={detail.estimate}
            block={scheduling?.block}
            open={!!scheduling}
            onClose={() => setScheduling(null)}
          />
        </PanelSection>
        <div className="h-8" />
      </div>
    </aside>
  )
}

function MinutesInput({ value, onSave }: { value: number; onSave: (m: number) => void }) {
  const [text, setText] = useState('')
  return (
    <input
      value={text}
      onChange={(e) => setText(e.target.value.replace(/[^0-9]/g, ''))}
      onKeyDown={(e) => e.key === 'Enter' && e.currentTarget.blur()}
      onBlur={() => {
        const m = Math.min(600, Math.max(5, Number(text)))
        if (text && m !== value) onSave(m)
        setText('')
      }}
      placeholder="min"
      aria-label="Minutes"
      className="h-6 w-12 rounded-md border border-line bg-surface px-1.5 text-[12.5px] text-fg outline-none placeholder:text-subtle focus-visible:border-accent focus-visible:outline-none"
    />
  )
}

export function TagsInput({ tags, onSave }: { tags: string[]; onSave: (t: string[]) => void }) {
  const [text, setText] = useState('')
  const add = () => {
    const more = text
      .split(/[,\s]+/)
      .map((t) => t.trim().replace(/^#/, ''))
      .filter((t) => t && !tags.includes(t))
    if (more.length) onSave([...tags, ...more])
    setText('')
  }
  return (
    <div className="flex flex-wrap items-center gap-1">
      {tags.map((t) => (
        <span key={t} className="group inline-flex h-6 items-center gap-0.5 rounded-md bg-hover pr-0.5 pl-2 text-[12.5px] text-muted">
          #{t}
          <button aria-label={`Remove ${t}`} onClick={() => onSave(tags.filter((x) => x !== t))} className="rounded p-0.5 text-subtle hover:text-danger">
            <X className="size-3" />
          </button>
        </span>
      ))}
      <input
        value={text}
        onChange={(e) => setText(e.target.value)}
        onKeyDown={(e) => {
          if (e.key === 'Enter' || e.key === ',') {
            e.preventDefault()
            add()
          } else if (e.key === 'Backspace' && !text && tags.length) onSave(tags.slice(0, -1))
        }}
        onBlur={add}
        placeholder={tags.length ? '' : 'phone, errands'}
        aria-label="Add tag"
        className="h-6 min-w-20 flex-1 bg-transparent px-1 text-[13px] text-fg outline-none placeholder:text-subtle focus-visible:outline-none"
      />
    </div>
  )
}

function StepsSection({ detail, ai }: { detail: TaskDetail; ai: AiState }) {
  const nav = useNav()
  const [proposed, setProposed] = useState<ProposedStep[]>([])
  const [explanation, setExplanation] = useState('')
  const [asking, setAsking] = useState(false)
  const [text, setText] = useState('')
  const done = detail.subtasks.filter((s) => s.status === 'completed' || s.status === 'cancelled').length
  const finished = detail.status === 'completed' || detail.status === 'cancelled'

  useSignal(
    'app',
    'stepsReady',
    useCallback(
      (id: string, steps: ProposedStep[], why: string) => {
        if (id !== detail.id) return
        setAsking(false)
        setProposed(steps)
        setExplanation(why)
      },
      [detail.id],
    ),
  )
  useSignal(
    'app',
    'aiFailed',
    useCallback((feature: string) => feature === 'breakdown' && setAsking(false), []),
  )

  return (
    <PanelSection title="Steps" aside={detail.subtasks.length ? `${done} of ${detail.subtasks.length} done` : undefined}>
      {detail.subtasks.length > 0 && (
        <div className="mb-1 h-1 overflow-hidden rounded-full bg-hover">
          <div className="h-full rounded-full bg-success transition-[width]" style={{ width: `${(done / detail.subtasks.length) * 100}%` }} />
        </div>
      )}
      {detail.subtasks.map((s) => (
        <div key={s.id} className="group flex min-h-8 items-center gap-2.5 rounded-lg px-1 hover:bg-hover">
          <Checkbox checked={s.status === 'completed'} label={s.title} onChange={(c) => call('app', 'setTaskStatus', s.id, c ? 'completed' : 'pending')} />
          <button onClick={() => nav.openTask(s.id)} className={clsx('flex-1 py-1 text-left text-[13.5px]', s.status === 'completed' ? 'text-subtle line-through' : 'text-fg')}>
            {s.title}
          </button>
          {s.estimate > 0 && <span className="text-[12px] text-subtle">{duration(s.estimate)}</span>}
          <ArrowUpRight className="size-3.5 text-subtle opacity-0 group-hover:opacity-100" />
        </div>
      ))}

      {proposed.length > 0 && (
        <div className="my-2 rounded-xl border border-accent/25 bg-accent-soft/50 p-3">
          <p className="mb-2 text-[12.5px] text-muted">{explanation || "Suggested steps. Untick any you don't want."}</p>
          {proposed.map((s, i) => (
            <label key={i} className="flex min-h-8 items-center gap-2.5 px-1">
              <Checkbox checked={s.checked} onChange={(c) => setProposed(proposed.map((p, j) => (j === i ? { ...p, checked: c } : p)))} />
              <span className="flex-1 text-[13.5px] text-fg">{s.title}</span>
              <span className="text-[12px] text-subtle">{duration(s.minutes)}</span>
            </label>
          ))}
          <div className="mt-2 flex gap-2">
            <Button
              size="sm"
              variant="primary"
              onClick={async () => {
                if (await call<boolean>('app', 'addSteps', detail.id, proposed.filter((p) => p.checked))) setProposed([])
              }}
            >
              Add the ticked steps
            </Button>
            <Button size="sm" variant="ghost" onClick={() => setProposed([])}>
              Discard
            </Button>
          </div>
        </div>
      )}

      <div className="mt-1 flex items-center gap-2.5 px-1">
        <Plus className="size-[18px] text-subtle" />
        <input
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={(e) => {
            const value = e.currentTarget.value.trim()
            if (e.key === 'Enter' && value) {
              call('app', 'addSubtask', detail.id, value)
              setText('')
            }
          }}
          placeholder="Add a step — small and concrete works best"
          className="h-8 flex-1 bg-transparent text-[13.5px] text-fg outline-none placeholder:text-subtle focus-visible:outline-none"
        />
        {ai.ready && proposed.length === 0 && !finished && (
          <Button
            size="sm"
            variant="ghost"
            disabled={asking}
            icon={<Sparkles className="size-3.5" />}
            onClick={() => {
              setAsking(true)
              call('app', 'breakDown', detail.id)
            }}
          >
            {asking ? `Asking ${ai.label ?? 'AI'}…` : 'Suggest steps'}
          </Button>
        )}
      </div>
    </PanelSection>
  )
}

function RepeatSection({ detail, update }: { detail: TaskDetail; update: (f: Record<string, unknown>) => void }) {
  const rec = detail.recurrence ?? null
  const save = (changes: Partial<Recurrence>) => {
    const next: Recurrence = { ...(rec ?? { type: 'daily', interval: 1 }), ...changes }
    if (next.type === 'weekly' && !next.days?.length) next.days = [new Date().getDay()]
    update({ recurrence: next })
  }
  return (
    <PanelSection title="Repeats" aside={<Toggle checked={!!rec} label="Repeats" onChange={(on) => (on ? save({}) : update({ recurrence: null }))} />}>
      {!rec ? (
        <p className="text-[13px] text-subtle">Just once.</p>
      ) : (
        <div className="flex flex-col gap-2">
          <div className="flex flex-wrap items-center gap-2 text-[13px] text-muted">
            Every
            <input
              type="number"
              min={1}
              max={52}
              value={rec.interval}
              onChange={(e) => Number(e.target.value) >= 1 && save({ interval: Math.min(52, Number(e.target.value)) })}
              className="h-7 w-14 rounded-lg border border-line bg-surface px-2 text-[13px] text-fg shadow-card outline-none focus-visible:border-accent"
            />
            <Segmented
              size="sm"
              value={rec.type}
              onChange={(type) => save({ type })}
              options={[
                { value: 'daily', label: rec.interval === 1 ? 'day' : 'days' },
                { value: 'weekly', label: rec.interval === 1 ? 'week' : 'weeks' },
              ]}
            />
            <span className="text-subtle">· {detail.recurrenceText}</span>
          </div>
          {rec.type === 'weekly' && (
            <div className="flex gap-1">
              {weekdays.map(({ day, label }) => {
                const on = rec.days?.includes(day) ?? false
                return (
                  <button
                    key={day}
                    aria-pressed={on}
                    onClick={() => {
                      const days = on ? (rec.days ?? []).filter((d) => d !== day) : [...(rec.days ?? []), day]
                      if (days.length) save({ days: days.sort() })
                    }}
                    className={clsx(
                      'h-7 w-11 rounded-lg text-[12.5px] font-medium transition-colors',
                      on ? 'bg-accent text-accent-fg' : 'bg-hover text-muted hover:text-fg',
                    )}
                  >
                    {label}
                  </button>
                )
              })}
            </div>
          )}
          <div className="flex items-center gap-2 text-[13px] text-muted">
            At
            <TimePicker value={detail.scheduledTime} onChange={(t) => update({ scheduled_time: t, is_fixed_time: t !== '' })} />
          </div>
        </div>
      )}
    </PanelSection>
  )
}

function WaitingSection({ detail }: { detail: TaskDetail }) {
  const nav = useNav()
  const candidates = useQuery<Task[]>('app', 'prerequisiteCandidates', [detail.id], [])
  const statusTone = (t: Task) => (t.status === 'completed' ? 'success' : 'neutral')
  return (
    <PanelSection title="Waiting on">
      {detail.prerequisites.map((p) => (
        <div key={p.id} className="group flex h-8 items-center gap-2 rounded-lg px-1 hover:bg-hover">
          <button onClick={() => nav.openTask(p.id)} className={clsx('flex-1 truncate text-left text-[13.5px]', p.status === 'completed' && 'text-subtle line-through')}>
            {p.title}
          </button>
          <Chip tone={statusTone(p)}>{statusLabel[p.status]}</Chip>
          <IconButton label="No longer waiting on this" size="sm" className="opacity-0 group-hover:opacity-100" onClick={() => call('app', 'removeDependency', detail.id, p.id)}>
            <X className="size-3.5" />
          </IconButton>
        </div>
      ))}
      <Select
        key={detail.prerequisites.length}
        size="sm"
        className="mt-1 w-full"
        value=""
        placeholder="Add something this waits on…"
        onChange={(id) => id && call('app', 'addDependency', detail.id, id)}
        options={candidates.map((c) => ({ value: c.id, label: c.title }))}
      />
      {detail.dependents.length > 0 && (
        <div className="mt-3">
          <div className="mb-1 text-[12px] font-medium text-subtle">Holding up</div>
          {detail.dependents.map((d) => (
            <button key={d.id} onClick={() => nav.openTask(d.id)} className="flex h-8 w-full items-center gap-2 rounded-lg px-1 text-left text-[13.5px] hover:bg-hover">
              <span className="flex-1 truncate">{d.title}</span>
              <ArrowUpRight className="size-3.5 text-subtle" />
            </button>
          ))}
        </div>
      )}
    </PanelSection>
  )
}

function NotesSection({ detail }: { detail: TaskDetail }) {
  const nav = useNav()
  const notes = useQuery<NoteSummary[]>('app', 'notes', ['', '', detail.id], [])
  return (
    <PanelSection title="Notes">
      {notes.map((n) => (
        <button key={n.id} onClick={() => nav.openNote(n.id)} className="flex w-full items-start gap-2.5 rounded-lg px-1 py-1.5 text-left hover:bg-hover">
          <StickyNote className="mt-0.5 size-4 shrink-0 text-subtle" />
          <span className="min-w-0 flex-1">
            <span className="block truncate text-[13.5px] text-fg">{n.title || 'Untitled'}</span>
            <span className="block truncate text-[12.5px] text-muted">{n.snippet.replace(/<[^>]+>/g, '')}</span>
          </span>
        </button>
      ))}
      <Button
        size="sm"
        variant="ghost"
        icon={<Plus className="size-3.5" />}
        className="-ml-1"
        onClick={async () => {
          const id = await call<string>('app', 'createNote', { title: detail.title, taskId: detail.id })
          if (id) nav.openNote(id)
        }}
      >
        Add a note
      </Button>
    </PanelSection>
  )
}

const kindIcon = { url: Globe, file: FileText, app: AppWindow, command: SquareTerminal }

export function ActionIcon({ action }: { action: Pick<TaskAction, 'kind' | 'icon'> }) {
  const [failed, setFailed] = useState(false)
  const Icon = kindIcon[action.kind] ?? ExternalLink
  if (action.kind === 'app' && action.icon && !failed)
    return <img src={`tumbler://app/icon/${encodeURIComponent(action.icon)}?size=64`} onError={() => setFailed(true)} className="size-5" alt="" />
  return <Icon className="size-4 text-muted" />
}

function ActionRow({ action }: { action: TaskAction }) {
  return (
    <div className="group flex min-h-9 items-center gap-2.5 rounded-lg px-1 hover:bg-hover">
      <span className="flex size-6 items-center justify-center">
        <ActionIcon action={action} />
      </span>
      <span className="min-w-0 flex-1">
        <span className="block truncate text-[13.5px] text-fg">{action.label}</span>
        <span className="block text-[12px] text-subtle">
          {action.onStart ? 'Opens when you start' : 'Only when you ask'}
          {action.needsApproval ? ' · asks once before running' : ''}
        </span>
      </span>
      <IconButton label="Open now" size="sm" onClick={() => call('app', 'runAction', action.id)}>
        <Play className="size-3.5" />
      </IconButton>
      <IconButton label="Remove" size="sm" className="opacity-0 group-hover:opacity-100" onClick={() => call('app', 'removeAction', action.id)}>
        <X className="size-3.5" />
      </IconButton>
    </div>
  )
}

