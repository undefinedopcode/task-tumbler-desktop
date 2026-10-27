import * as P from '@radix-ui/react-popover'
import clsx from 'clsx'
import { Clock } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { time } from '../lib/format'

function label(hhmm: string): string {
  const [h, m] = hhmm.split(':').map(Number)
  const d = new Date()
  d.setHours(h, m, 0, 0)
  return time(d.getTime())
}

// "17:30", "5:30pm", "530", "9" → "HH:MM", or null.
function parseTime(text: string): string | null {
  const t = text.trim().toLowerCase().replace(/\s+/g, '')
  const m = t.match(/^(\d{1,2})(?::?(\d{2}))?(am|pm|a|p)?$/)
  if (!m) return null
  let h = Number(m[1])
  const min = Number(m[2] ?? 0)
  if (m[3]?.startsWith('p') && h < 12) h += 12
  if (m[3]?.startsWith('a') && h === 12) h = 0
  if (h > 23 || min > 59) return null
  return `${String(h).padStart(2, '0')}:${String(min).padStart(2, '0')}`
}

const slots = Array.from({ length: 48 }, (_, i) => `${String(Math.floor(i / 2)).padStart(2, '0')}:${i % 2 ? '30' : '00'}`)

// A time of day ("HH:MM", or "" for none) as a button with a list of times
// and a box to type one.
export function TimePicker({ value, onChange, placeholder = 'Any time' }: { value: string; onChange: (hhmm: string) => void; placeholder?: string }) {
  const [open, setOpen] = useState(false)
  const [text, setText] = useState('')
  const list = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!open) return
    setText('')
    // Start the list near the current value (or the morning).
    requestAnimationFrame(() => {
      const target = list.current?.querySelector(`[data-time="${value || '08:00'}"]`) as HTMLElement | null
      target?.scrollIntoView({ block: 'center' })
    })
  }, [open, value])
  const pick = (v: string) => {
    onChange(v)
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
        <Clock className="size-3.5 text-subtle" />
        {value ? label(value) : placeholder}
      </P.Trigger>
      <P.Portal>
        <P.Content align="start" sideOffset={4} className="z-50 w-44 animate-pop-in rounded-xl border border-line bg-surface p-1.5 shadow-pop outline-none">
          <input
            autoFocus
            value={text}
            onChange={(e) => setText(e.target.value)}
            onKeyDown={(e) => {
              const v = parseTime(e.currentTarget.value)
              if (e.key === 'Enter' && v) pick(v)
            }}
            placeholder="Type a time…"
            className="mb-1 h-7 w-full rounded-md border border-line bg-surface px-2 text-[13px] text-fg outline-none placeholder:text-subtle focus-visible:border-accent focus-visible:outline-none"
          />
          <div ref={list} className="max-h-56 overflow-y-auto">
            {value && (
              <button onClick={() => pick('')} className="flex h-7 w-full items-center rounded-md px-2 text-left text-[13px] text-subtle hover:bg-hover hover:text-danger">
                {placeholder}
              </button>
            )}
            {slots.map((s) => (
              <button
                key={s}
                data-time={s}
                onClick={() => pick(s)}
                className={clsx(
                  'flex h-7 w-full items-center rounded-md px-2 text-left text-[13px] tabular-nums',
                  s === value ? 'bg-accent text-accent-fg' : 'text-fg hover:bg-hover',
                )}
              >
                {label(s)}
              </button>
            ))}
          </div>
        </P.Content>
      </P.Portal>
    </P.Root>
  )
}
