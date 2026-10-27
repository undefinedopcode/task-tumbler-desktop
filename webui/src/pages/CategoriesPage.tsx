import clsx from 'clsx'
import { Pencil, Plus, Trash2 } from 'lucide-react'
import { useState } from 'react'
import { call, useProp } from '../bridge/hooks'
import type { Category, CategoryDay } from '../bridge/types'
import { Dialog, TextField } from '../components/Dialog'
import { TimePicker } from '../components/TimePicker'
import { Button, Checkbox, Chip, IconButton, Toggle } from '../components/ui'

const swatches = ['#6366f1', '#0ea5e9', '#10b981', '#84cc16', '#f59e0b', '#f97316', '#ef4444', '#ec4899', '#a855f7', '#64748b']
const mondayFirst = [1, 2, 3, 4, 5, 6, 0]
const dayName = (d: number, style: 'short' | 'long' = 'short') => new Date(2024, 0, 7 + d).toLocaleDateString(undefined, { weekday: style })

export function CategoriesPage() {
  const categories = useProp<Category[]>('app', 'categories', [])
  const [editing, setEditing] = useState<Category | 'new' | null>(null)
  return (
    <div className="mx-auto flex w-full max-w-[820px] flex-col gap-4 px-8 pt-8 pb-16">
      <header className="flex items-end gap-4">
        <div className="flex-1">
          <h1 className="text-[26px] leading-tight font-semibold tracking-tight">Categories</h1>
          <p className="mt-1 max-w-xl text-[13.5px] text-muted">
            Each category has its own hours. A task is only ever scheduled inside its category's hours, so work stays in work time.
          </p>
        </div>
        <Button variant="primary" icon={<Plus className="size-4" />} onClick={() => setEditing('new')}>
          New category
        </Button>
      </header>
      {categories.map((c) => (
        <div key={c.id} className="group flex items-start gap-4 rounded-xl border border-line bg-surface p-4 shadow-card">
          <span className="mt-1 size-3.5 shrink-0 rounded-full" style={{ background: c.color }} />
          <div className="min-w-0 flex-1">
            <div className="flex items-center gap-2">
              <span className="text-[15px] font-semibold text-fg">{c.name}</span>
              {c.isDefault && <Chip tone="accent">Default</Chip>}
            </div>
            <div className="mt-2 flex flex-wrap gap-1.5">
              {mondayFirst.map((d) => {
                const day = c.days?.find((x) => x.day === d)
                return (
                  <span
                    key={d}
                    className={clsx('inline-flex h-6 items-center gap-1 rounded-md px-2 text-[12px] tabular-nums', day?.enabled ? 'font-medium text-fg' : 'text-subtle')}
                    style={{ background: day?.enabled ? `color-mix(in srgb, ${c.color} 14%, transparent)` : 'var(--tt-hover)' }}
                  >
                    {dayName(d)} {day?.enabled ? `${day.start}–${day.end}` : 'off'}
                  </span>
                )
              })}
            </div>
          </div>
          <div className="flex gap-1 opacity-60 transition-opacity group-hover:opacity-100">
            <IconButton label="Edit" size="sm" onClick={() => setEditing(c)}>
              <Pencil className="size-4" />
            </IconButton>
            {!c.isDefault && (
              <IconButton label="Delete; its tasks move to the default category" size="sm" onClick={() => call('app', 'deleteCategory', c.id)}>
                <Trash2 className="size-4" />
              </IconButton>
            )}
          </div>
        </div>
      ))}
      {editing && <CategoryDialog category={editing === 'new' ? null : editing} onClose={() => setEditing(null)} />}
    </div>
  )
}

// A category's name, colour and the hours on each weekday when its tasks may
// be scheduled.
function CategoryDialog({ category, onClose }: { category: Category | null; onClose: () => void }) {
  const [c, setC] = useState<Category>(() => (category ? structuredClone(category) : { id: '', name: '', color: swatches[0], isDefault: false, showOnSchedule: true }))
  const [days, setDays] = useState<CategoryDay[]>(() => {
    const src = category?.days ?? [0, 1, 2, 3, 4, 5, 6].map((d) => ({ day: d, enabled: d >= 1 && d <= 5, start: '09:00', end: '17:00' }))
    return mondayFirst.map((d) => ({ ...(src.find((x) => x.day === d) ?? { day: d, enabled: false, start: '09:00', end: '17:00' }) }))
  })
  const setDay = (i: number, patch: Partial<CategoryDay>) => setDays(days.map((d, j) => (j === i ? { ...d, ...patch } : d)))
  const save = async () => {
    const id = await call<string>('app', 'saveCategory', { ...c, name: c.name.trim(), days })
    if (id) onClose()
  }
  return (
    <Dialog
      open
      onOpenChange={(o) => !o && onClose()}
      title={category ? 'Edit category' : 'New category'}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button variant="primary" disabled={!c.name.trim()} onClick={save}>
            Save
          </Button>
        </>
      }
    >
      <TextField autoFocus value={c.name} onChange={(e) => setC({ ...c, name: e.target.value })} placeholder="Work, Home, Side project…" className="h-9 text-[15px]" />
      <div className="mt-3 flex flex-wrap gap-1.5">
        {swatches.map((s) => (
          <button
            key={s}
            aria-label={s}
            onClick={() => setC({ ...c, color: s })}
            className={clsx('size-6 rounded-full transition-transform hover:scale-110', c.color === s && 'ring-2 ring-fg ring-offset-2 ring-offset-surface')}
            style={{ background: s }}
          />
        ))}
      </div>
      <div className="mt-4 flex flex-col gap-2">
        <label className="flex items-center gap-3 text-[13.5px] text-fg">
          <Toggle checked={c.isDefault} onChange={(v) => (!category?.isDefault || !c.id) && setC({ ...c, isDefault: v })} />
          Default for new tasks
        </label>
        <label className="flex items-center gap-3 text-[13.5px] text-fg">
          <Toggle checked={c.showOnSchedule} onChange={(v) => setC({ ...c, showOnSchedule: v })} />
          Shade its hours on the schedule
        </label>
      </div>
      <div className="mt-5 mb-2 text-[12.5px] font-semibold tracking-wide text-muted uppercase">When its tasks can be scheduled</div>
      <div className="flex flex-col gap-1">
        {days.map((d, i) => (
          <div key={d.day} className="flex h-9 items-center gap-3">
            <label className="flex w-28 items-center gap-2.5 text-[13.5px] text-fg">
              <Checkbox checked={d.enabled} onChange={(v) => setDay(i, { enabled: v })} />
              {dayName(d.day, 'long')}
            </label>
            {d.enabled ? (
              <>
                <TimePicker value={d.start} onChange={(v) => v && setDay(i, { start: v })} />
                <span className="text-[13px] text-subtle">to</span>
                <TimePicker value={d.end} onChange={(v) => v && setDay(i, { end: v })} />
              </>
            ) : (
              <span className="text-[13px] text-subtle">Off</span>
            )}
          </div>
        ))}
      </div>
    </Dialog>
  )
}
