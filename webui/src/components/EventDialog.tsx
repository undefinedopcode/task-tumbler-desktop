import clsx from 'clsx'
import { Lock, Trash2 } from 'lucide-react'
import { useEffect, useState } from 'react'
import { call } from '../bridge/hooks'
import type { BlockingMode, EventDetail } from '../bridge/types'
import { DatePicker, isoDate } from './DatePicker'
import { Dialog, TextField } from './Dialog'
import { Field } from './fields'
import { Select } from './Select'
import { TimePicker } from './TimePicker'
import { Button, Toggle } from './ui'

const swatches = ['#6366f1', '#0ea5e9', '#10b981', '#f59e0b', '#ef4444', '#ec4899', '#a855f7', '#64748b']
const modes: { value: BlockingMode; label: string }[] = [
  { value: 'none', label: 'Anything can be scheduled' },
  { value: 'block_all', label: 'Nothing else during it' },
  { value: 'recurring_only', label: 'Only routines' },
  { value: 'tags_only', label: 'Only tasks with certain tags' },
]

function hhmm(d: Date): string {
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
}

function blank(start?: number): EventDetail {
  const s = start ? new Date(start) : new Date()
  if (!start) s.setHours(s.getHours() + 1, 0, 0, 0)
  const e = new Date(s.getTime() + 3_600_000)
  return {
    id: '',
    title: '',
    allDay: false,
    date: isoDate(s),
    endDate: isoDate(e),
    startTime: hhmm(s),
    endTime: hhmm(e),
    blockingMode: 'block_all',
    allowedTags: [],
    color: swatches[0],
    location: '',
  }
}

// Create or edit an event, including what may be scheduled during it.
export function EventDialog({ id, start, onClose }: { id: string; start?: number; onClose: () => void }) {
  const [event, setEvent] = useState<EventDetail | null>(id ? null : blank(start))
  const [tags, setTags] = useState('')
  useEffect(() => {
    if (id)
      call<EventDetail>('app', 'eventDetail', id).then((e) => {
        setEvent(e)
        setTags((e.allowedTags ?? []).join(', '))
      })
  }, [id])
  if (!event) return null
  // From a subscribed calendar: shown, not edited (the next sync would undo it).
  const readOnly = !!event.feed
  const set = (patch: Partial<EventDetail>) => setEvent({ ...event, ...patch })
  const save = async () => {
    const allowedTags = tags
      .split(',')
      .map((t) => t.trim().replace(/^#/, ''))
      .filter(Boolean)
    const saved = await call<string>('app', 'saveEvent', { ...event, allowedTags })
    if (saved) onClose()
  }

  return (
    <Dialog
      open
      onOpenChange={(o) => !o && onClose()}
      title={readOnly ? event.title : event.id ? 'Edit event' : 'New event'}
      description={
        readOnly ? (
          <span className="flex items-center gap-1.5">
            <Lock className="size-3.5" /> From “{event.feed}”. Change it there; the next sync would undo edits here.
          </span>
        ) : undefined
      }
      footer={
        readOnly ? (
          <Button onClick={onClose}>Close</Button>
        ) : (
          <>
            {event.id && (
              <Button
                variant="ghost"
                className="mr-auto text-danger hover:bg-danger-soft hover:text-danger"
                icon={<Trash2 className="size-4" />}
                onClick={async () => {
                  await call('app', 'deleteEvent', event.id)
                  onClose()
                }}
              >
                Delete
              </Button>
            )}
            <Button variant="ghost" onClick={onClose}>
              Cancel
            </Button>
            <Button variant="primary" disabled={!event.title.trim()} onClick={save}>
              Save
            </Button>
          </>
        )
      }
    >
      <fieldset disabled={readOnly} className="flex flex-col gap-1">
        <TextField
          autoFocus
          value={event.title}
          onChange={(e) => set({ title: e.target.value })}
          onKeyDown={(e) => e.key === 'Enter' && event.title.trim() && save()}
          placeholder="Physio, school run, flight…"
          className="mb-2 h-9 text-[15px]"
        />
        <Field label="All day">
          <Toggle checked={event.allDay} onChange={(allDay) => set({ allDay })} />
        </Field>
        <Field label={event.allDay ? 'From' : 'When'}>
          <div className="flex flex-wrap items-center gap-2">
            <DatePicker
              value={event.date}
              clearable={false}
              onChange={(date) => {
                // Moving the start date moves the end date with it.
                const shift = new Date(`${event.endDate}T00:00:00`).getTime() - new Date(`${event.date}T00:00:00`).getTime()
                set({ date, endDate: isoDate(new Date(new Date(`${date}T00:00:00`).getTime() + (isNaN(shift) ? 0 : shift))) })
              }}
            />
            {!event.allDay && (
              <>
                <TimePicker value={event.startTime} placeholder="Start" onChange={(startTime) => startTime && set({ startTime })} />
                <span className="text-[13px] text-subtle">to</span>
                <TimePicker value={event.endTime} placeholder="End" onChange={(endTime) => endTime && set({ endTime, endDate: event.date })} />
              </>
            )}
          </div>
        </Field>
        {event.allDay && (
          <Field label="Until">
            <DatePicker value={event.endDate} clearable={false} onChange={(endDate) => endDate && set({ endDate })} />
          </Field>
        )}
        <Field label="During it">
          <Select size="sm" value={event.blockingMode} onChange={(v) => set({ blockingMode: v as BlockingMode })} options={modes} />
        </Field>
        {event.blockingMode === 'tags_only' && (
          <Field label="Allowed tags">
            <TextField value={tags} onChange={(e) => setTags(e.target.value)} placeholder="phone, reading" />
          </Field>
        )}
        <Field label="Where">
          <TextField value={event.location} onChange={(e) => set({ location: e.target.value })} placeholder="Optional" />
        </Field>
        <Field label="Colour">
          <div className="flex gap-1.5">
            {swatches.map((c) => (
              <button
                key={c}
                aria-label={c}
                onClick={() => set({ color: c })}
                className={clsx('size-6 rounded-full transition-transform hover:scale-110', event.color === c && 'ring-2 ring-fg ring-offset-2 ring-offset-surface')}
                style={{ background: c }}
              />
            ))}
          </div>
        </Field>
      </fieldset>
    </Dialog>
  )
}
