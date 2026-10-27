import clsx from 'clsx'
import type { ReactNode } from 'react'

// A row of mutually exclusive choices.
export function Segmented<T extends string>({
  value,
  options,
  onChange,
  size = 'md',
  className,
}: {
  value: T
  options: { value: T; label: ReactNode; title?: string }[]
  onChange: (v: T) => void
  size?: 'sm' | 'md'
  className?: string
}) {
  return (
    <div role="radiogroup" className={clsx('inline-flex rounded-lg bg-hover p-0.5', className)}>
      {options.map((o) => (
        <button
          key={o.value}
          role="radio"
          aria-checked={value === o.value}
          title={o.title}
          onClick={() => onChange(o.value)}
          className={clsx(
            'rounded-md px-2.5 font-medium whitespace-nowrap transition-colors',
            size === 'sm' ? 'h-6 text-[12.5px]' : 'h-7 text-[13px]',
            value === o.value ? 'bg-surface text-fg shadow-card' : 'text-muted hover:text-fg',
          )}
        >
          {o.label}
        </button>
      ))}
    </div>
  )
}
