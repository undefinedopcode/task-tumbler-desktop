import clsx from 'clsx'
import { forwardRef, type ButtonHTMLAttributes, type ReactNode } from 'react'
import * as Tooltip from '@radix-ui/react-tooltip'

type Variant = 'primary' | 'secondary' | 'ghost' | 'soft'

export const Button = forwardRef<
  HTMLButtonElement,
  ButtonHTMLAttributes<HTMLButtonElement> & { variant?: Variant; size?: 'sm' | 'md'; icon?: ReactNode }
>(function Button({ variant = 'secondary', size = 'md', icon, className, children, ...rest }, ref) {
  return (
    <button
      ref={ref}
      className={clsx(
        'inline-flex shrink-0 items-center justify-center gap-1.5 rounded-lg font-medium whitespace-nowrap transition-colors select-none',
        'disabled:pointer-events-none disabled:opacity-45',
        size === 'sm' ? 'h-7 px-2.5 text-[13px]' : 'h-8 px-3 text-[13.5px]',
        variant === 'primary' && 'bg-accent text-accent-fg shadow-card hover:bg-accent-hover active:brightness-95',
        variant === 'secondary' && 'border border-line bg-surface text-fg shadow-card hover:bg-raised active:bg-press',
        variant === 'ghost' && 'text-muted hover:bg-hover hover:text-fg active:bg-press',
        variant === 'soft' && 'bg-accent-soft text-accent-text hover:brightness-[0.97] active:brightness-95',
        className,
      )}
      {...rest}
    >
      {icon}
      {children}
    </button>
  )
})

export const IconButton = forwardRef<
  HTMLButtonElement,
  ButtonHTMLAttributes<HTMLButtonElement> & { label: string; size?: 'sm' | 'md'; tone?: 'default' | 'success' }
>(function IconButton({ label, size = 'md', tone = 'default', className, children, ...rest }, ref) {
  return (
    <Tip label={label}>
      <button
        ref={ref}
        aria-label={label}
        className={clsx(
          'inline-flex shrink-0 items-center justify-center rounded-lg text-muted transition-colors',
          'hover:bg-hover hover:text-fg active:bg-press disabled:pointer-events-none disabled:opacity-40',
          tone === 'success' && 'hover:bg-success-soft hover:text-success',
          size === 'sm' ? 'size-7' : 'size-8',
          className,
        )}
        {...rest}
      >
        {children}
      </button>
    </Tip>
  )
})

export function Tip({ label, children, side = 'top' }: { label: ReactNode; children: ReactNode; side?: 'top' | 'bottom' | 'right' }) {
  return (
    <Tooltip.Root delayDuration={500}>
      <Tooltip.Trigger asChild>{children}</Tooltip.Trigger>
      <Tooltip.Portal>
        <Tooltip.Content
          side={side}
          sideOffset={6}
          className="z-50 animate-pop-in rounded-md bg-fg px-2 py-1 text-xs font-medium text-bg shadow-pop"
        >
          {label}
        </Tooltip.Content>
      </Tooltip.Portal>
    </Tooltip.Root>
  )
}

export function Chip({
  children,
  tone = 'neutral',
  icon,
  className,
}: {
  children: ReactNode
  tone?: 'neutral' | 'accent' | 'danger' | 'warning' | 'success'
  icon?: ReactNode
  className?: string
}) {
  return (
    <span
      className={clsx(
        'inline-flex h-5 items-center gap-1 rounded-md px-1.5 text-[11.5px] font-medium whitespace-nowrap',
        tone === 'neutral' && 'bg-hover text-muted',
        tone === 'accent' && 'bg-accent-soft text-accent-text',
        tone === 'danger' && 'bg-danger-soft text-danger',
        tone === 'warning' && 'bg-warning-soft text-warning',
        tone === 'success' && 'bg-success-soft text-success',
        className,
      )}
    >
      {icon}
      {children}
    </span>
  )
}

export function Card({ className, children }: { className?: string; children: ReactNode }) {
  return <div className={clsx('rounded-xl border border-line bg-surface shadow-card', className)}>{children}</div>
}

export function SectionHeader({ title, count, action }: { title: string; count?: number; action?: ReactNode }) {
  return (
    <div className="mb-2 flex h-7 items-center gap-2 px-1">
      <h2 className="text-[12.5px] font-semibold tracking-wide text-muted uppercase">{title}</h2>
      {count !== undefined && <span className="text-[12px] font-medium text-subtle tabular-nums">{count}</span>}
      <div className="ml-auto">{action}</div>
    </div>
  )
}

export function Empty({ icon, title, hint }: { icon: ReactNode; title: string; hint?: string }) {
  return (
    <div className="flex flex-col items-center justify-center rounded-xl border border-dashed border-line px-6 py-8 text-center">
      <div className="mb-2 text-subtle">{icon}</div>
      <div className="text-[13.5px] font-medium text-fg">{title}</div>
      {hint && <div className="mt-0.5 text-[12.5px] text-muted">{hint}</div>}
    </div>
  )
}

// A coloured dot for a category or calendar colour.
export function Dot({ color, className }: { color?: string; className?: string }) {
  return <span className={clsx('inline-block size-2 shrink-0 rounded-full', className)} style={{ background: color || 'var(--tt-subtle)' }} />
}

export function Kbd({ children }: { children: ReactNode }) {
  return (
    <kbd className="inline-flex h-5 min-w-5 items-center justify-center rounded border border-line bg-raised px-1 font-sans text-[11px] font-medium text-subtle">
      {children}
    </kbd>
  )
}
