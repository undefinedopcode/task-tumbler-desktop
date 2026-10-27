import clsx from 'clsx'
import { Minus, Plus } from 'lucide-react'
import { useEffect, useState, type ReactNode } from 'react'

// A titled card of settings rows.
export function Section({ id, title, description, children }: { id: string; title: string; description?: string; children: ReactNode }) {
  return (
    <section id={id} className="scroll-mt-6">
      <h2 className="mb-1 px-1 text-[15px] font-semibold text-fg">{title}</h2>
      {description && <p className="mb-2 px-1 text-[13px] text-muted">{description}</p>}
      <div className="divide-y divide-line rounded-xl border border-line bg-surface shadow-card">{children}</div>
    </section>
  )
}

// Label and explanation on the left, the control on the right (or below,
// for wide controls).
export function Row({ label, description, children, stacked, tone }: { label: ReactNode; description?: ReactNode; children?: ReactNode; stacked?: boolean; tone?: 'danger' }) {
  return (
    <div className={clsx('px-4 py-3', stacked ? 'flex flex-col gap-2' : 'flex items-center gap-4')}>
      <div className="min-w-0 flex-1">
        <div className={clsx('text-[13.5px] font-medium', tone === 'danger' ? 'text-danger' : 'text-fg')}>{label}</div>
        {description && <div className="mt-0.5 text-[12.5px] leading-snug text-muted">{description}</div>}
      </div>
      {children && <div className={clsx(stacked ? 'w-full' : 'shrink-0')}>{children}</div>}
    </div>
  )
}

// A whole number with − and + either side.
export function Stepper({ value, onChange, min = 0, max = 999, step = 1, suffix }: { value: number; onChange: (v: number) => void; min?: number; max?: number; step?: number; suffix?: string }) {
  const [text, setText] = useState(String(value))
  useEffect(() => setText(String(value)), [value])
  const commit = (v: number) => {
    const c = Math.min(max, Math.max(min, Math.round(v)))
    setText(String(c))
    if (c !== value) onChange(c)
  }
  return (
    <div className="inline-flex h-8 items-center rounded-lg border border-line bg-surface shadow-card">
      <button aria-label="Less" disabled={value <= min} onClick={() => commit(value - step)} className="flex h-full w-8 items-center justify-center text-muted hover:text-fg disabled:opacity-40">
        <Minus className="size-3.5" />
      </button>
      <input
        value={text}
        onChange={(e) => setText(e.target.value.replace(/[^0-9]/g, ''))}
        onBlur={() => commit(Number(text) || 0)}
        onKeyDown={(e) => e.key === 'Enter' && e.currentTarget.blur()}
        className="h-full w-10 bg-transparent text-center text-[13.5px] text-fg tabular-nums outline-none focus-visible:outline-none"
      />
      {suffix && <span className="pr-1 text-[12.5px] text-subtle">{suffix}</span>}
      <button aria-label="More" disabled={value >= max} onClick={() => commit(value + step)} className="flex h-full w-8 items-center justify-center text-muted hover:text-fg disabled:opacity-40">
        <Plus className="size-3.5" />
      </button>
    </div>
  )
}

// One line of text saved when you leave it.
export function TextSetting({ value, onSave, placeholder, mono, type = 'text', className }: { value: string; onSave: (v: string) => void; placeholder?: string; mono?: boolean; type?: string; className?: string }) {
  const [text, setText] = useState(value)
  useEffect(() => setText(value), [value])
  return (
    <input
      type={type}
      value={text}
      placeholder={placeholder}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => text !== value && onSave(text.trim())}
      onKeyDown={(e) => e.key === 'Enter' && e.currentTarget.blur()}
      className={clsx(
        'h-8 w-full rounded-lg border border-line bg-surface px-2.5 text-[13.5px] text-fg shadow-card outline-none placeholder:text-subtle',
        'focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none',
        mono && 'font-mono text-[12.5px]',
        className,
      )}
    />
  )
}

export function Code({ children }: { children: ReactNode }) {
  return <code className="rounded bg-hover px-1.5 py-0.5 font-mono text-[12px] text-fg select-text">{children}</code>
}
