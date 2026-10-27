import * as P from '@radix-ui/react-popover'
import clsx from 'clsx'
import { CalendarDays, ChevronLeft, ChevronRight } from 'lucide-react'
import { useState } from 'react'

export function isoDate(d: Date): string {
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`
}

function parse(iso: string): Date | null {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(iso)) return null
  return new Date(`${iso}T00:00:00`)
}

// "Wed 29 Oct", with the year only when it isn't this one.
export function dateLabel(iso: string): string {
  const d = parse(iso)
  if (!d) return ''
  return d.toLocaleDateString(undefined, {
    weekday: 'short',
    day: 'numeric',
    month: 'short',
    year: d.getFullYear() === new Date().getFullYear() ? undefined : 'numeric',
  })
}

// A month grid, Monday first.
export function Calendar({ value, onChange }: { value: string; onChange: (iso: string) => void }) {
  const selected = parse(value)
  const [month, setMonth] = useState(() => {
    const d = selected ?? new Date()
    return new Date(d.getFullYear(), d.getMonth(), 1)
  })
  const today = isoDate(new Date())
  const lead = (month.getDay() + 6) % 7
  const days = new Date(month.getFullYear(), month.getMonth() + 1, 0).getDate()
  const cells: (Date | null)[] = [...Array(lead).fill(null), ...Array.from({ length: days }, (_, i) => new Date(month.getFullYear(), month.getMonth(), i + 1))]
  while (cells.length % 7) cells.push(null)

  return (
    <div className="w-[252px] select-none">
      <div className="mb-2 flex items-center">
        <button aria-label="Previous month" onClick={() => setMonth(new Date(month.getFullYear(), month.getMonth() - 1, 1))} className="rounded-md p-1 text-muted hover:bg-hover hover:text-fg">
          <ChevronLeft className="size-4" />
        </button>
        <div className="flex-1 text-center text-[13px] font-semibold text-fg">
          {month.toLocaleDateString(undefined, { month: 'long', year: 'numeric' })}
        </div>
        <button aria-label="Next month" onClick={() => setMonth(new Date(month.getFullYear(), month.getMonth() + 1, 1))} className="rounded-md p-1 text-muted hover:bg-hover hover:text-fg">
          <ChevronRight className="size-4" />
        </button>
      </div>
      <div className="grid grid-cols-7 gap-0.5 text-center">
        {['M', 'T', 'W', 'T', 'F', 'S', 'S'].map((d, i) => (
          <div key={i} className="pb-1 text-[11px] font-medium text-subtle">
            {d}
          </div>
        ))}
        {cells.map((d, i) => {
          if (!d) return <div key={i} />
          const iso = isoDate(d)
          return (
            <button
              key={i}
              onClick={() => onChange(iso)}
              className={clsx(
                'h-8 rounded-lg text-[12.5px] tabular-nums transition-colors',
                iso === value ? 'bg-accent font-semibold text-accent-fg' : iso === today ? 'font-semibold text-accent-text hover:bg-hover' : 'text-fg hover:bg-hover',
                iso < today && iso !== value && 'text-subtle',
              )}
            >
              {d.getDate()}
            </button>
          )
        })}
      </div>
    </div>
  )
}

// A date as a button that opens a calendar, with quick picks.
export function DatePicker({
  value,
  onChange,
  placeholder = 'No date',
  quick = [
    { label: 'Today', days: 0 },
    { label: 'Tomorrow', days: 1 },
    { label: 'Next week', days: 7 },
  ],
  clearable = true,
}: {
  value: string
  onChange: (iso: string) => void
  placeholder?: string
  quick?: { label: string; days: number }[]
  clearable?: boolean
}) {
  const [open, setOpen] = useState(false)
  const pick = (iso: string) => {
    onChange(iso)
    setOpen(false)
  }
  return (
    <P.Root open={open} onOpenChange={setOpen}>
      <P.Trigger
        className={clsx(
          'inline-flex h-7 items-center gap-1.5 rounded-lg border border-line bg-surface px-2 text-[13px] shadow-card outline-none hover:bg-raised',
          'focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft',
          value ? 'text-fg' : 'text-subtle',
        )}
      >
        <CalendarDays className="size-3.5 text-subtle" />
        {value ? dateLabel(value) : placeholder}
      </P.Trigger>
      <P.Portal>
        <P.Content align="start" sideOffset={4} className="z-50 animate-pop-in rounded-xl border border-line bg-surface p-3 shadow-pop outline-none">
          <div className="mb-3 flex flex-wrap gap-1">
            {quick.map((q) => {
              const d = new Date()
              d.setDate(d.getDate() + q.days)
              return (
                <button key={q.label} onClick={() => pick(isoDate(d))} className="h-6 rounded-md bg-hover px-2 text-[12.5px] font-medium text-muted hover:text-fg">
                  {q.label}
                </button>
              )
            })}
            {clearable && value && (
              <button onClick={() => pick('')} className="ml-auto h-6 rounded-md px-2 text-[12.5px] text-subtle hover:text-danger">
                Clear
              </button>
            )}
          </div>
          <Calendar value={value} onChange={pick} />
        </P.Content>
      </P.Portal>
    </P.Root>
  )
}
