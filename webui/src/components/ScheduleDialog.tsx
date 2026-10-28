import { CalendarPlus, Clock } from 'lucide-react'
import { useEffect, useState } from 'react'
import { call } from '../bridge/hooks'
import type { ScheduleBlock } from '../bridge/types'
import { DatePicker, isoDate } from './DatePicker'
import { Dialog, TextField } from './Dialog'
import { Field } from './fields'
import { TimePicker } from './TimePicker'
import { Menu, MenuContent, MenuItem, MenuTrigger } from './Menu'
import { Button, IconButton } from './ui'

function hhmm(d: Date): string {
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`
}

// Put a task on the schedule at a chosen day and time, or move one of its
// blocks there.
export function ScheduleDialog({
  taskId,
  title,
  estimate,
  block,
  open,
  onClose,
}: {
  taskId: string
  title: string
  estimate: number
  block?: ScheduleBlock
  open: boolean
  onClose: () => void
}) {
  const [date, setDate] = useState('')
  const [time, setTime] = useState('')
  const [minutes, setMinutes] = useState('')
  useEffect(() => {
    if (!open) return
    // A new block starts at the next whole hour.
    const start = block ? new Date(block.start) : new Date()
    if (!block) start.setHours(start.getHours() + 1, 0, 0, 0)
    setDate(isoDate(start))
    setTime(hhmm(start))
    setMinutes(String(block ? Math.round((block.end - block.start) / 60000) : Math.max(5, estimate || 30)))
  }, [open, block, estimate])

  const start = date && time ? new Date(`${date}T${time}:00`).getTime() : NaN
  const length = Math.min(600, Math.max(5, Number(minutes) || 0))
  const save = async () => {
    if (isNaN(start)) return
    const ok = block ? await call<boolean>('app', 'reschedule', block.id, start, length) : await call<boolean>('app', 'scheduleAt', taskId, start, length)
    if (ok) onClose()
  }

  return (
    <Dialog
      open={open}
      onOpenChange={(o) => !o && onClose()}
      width="sm"
      title={block ? 'Move on the schedule' : 'Schedule'}
      description={title}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button variant="primary" disabled={isNaN(start)} onClick={save}>
            {block ? 'Move' : 'Schedule'}
          </Button>
        </>
      }
    >
      <div className="flex flex-col gap-1">
        <Field label="Day">
          <DatePicker value={date} clearable={false} onChange={(d) => d && setDate(d)} />
        </Field>
        <Field label="Time">
          <TimePicker value={time} placeholder="Start" onChange={(t) => t && setTime(t)} />
        </Field>
        <Field label="Length">
          <div className="flex items-center gap-2">
            <div className="w-20">
              <TextField
                value={minutes}
                onChange={(e) => setMinutes(e.target.value.replace(/[^0-9]/g, ''))}
                onKeyDown={(e) => e.key === 'Enter' && save()}
                aria-label="Minutes"
              />
            </div>
            <span className="text-[13px] text-subtle">min</span>
          </div>
        </Field>
        {!isNaN(start) && start < Date.now() && <p className="mt-1 text-[12.5px] text-subtle">That's in the past; it will show as overdue.</p>}
      </div>
    </Dialog>
  )
}

// The calendar button on a task: the next free slot, or a day and time.
export function ScheduleMenu({ taskId, title, estimate }: { taskId: string; title: string; estimate: number }) {
  const [open, setOpen] = useState(false)
  return (
    <>
      <Menu>
        <MenuTrigger asChild>
          <IconButton label="Schedule" size="sm">
            <CalendarPlus className="size-4" />
          </IconButton>
        </MenuTrigger>
        <MenuContent>
          <MenuItem icon={<CalendarPlus />} onSelect={() => call('app', 'scheduleNextSlot', taskId)}>
            Next free slot
          </MenuItem>
          <MenuItem icon={<Clock />} onSelect={() => setOpen(true)}>
            Pick a day and time…
          </MenuItem>
        </MenuContent>
      </Menu>
      <ScheduleDialog taskId={taskId} title={title} estimate={estimate} open={open} onClose={() => setOpen(false)} />
    </>
  )
}
