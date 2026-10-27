import clsx from 'clsx'
import type { ReactNode } from 'react'

// A gentle prompt across the top of a page, with its actions.
export function Banner({
  icon,
  tone = 'accent',
  children,
  actions,
}: {
  icon: ReactNode
  tone?: 'accent' | 'success' | 'warning'
  children: ReactNode
  actions?: ReactNode
}) {
  return (
    <div
      className={clsx(
        'flex animate-pop-in items-center gap-3 rounded-xl border px-4 py-3',
        tone === 'accent' && 'border-accent/25 bg-accent-soft',
        tone === 'success' && 'border-success/25 bg-success-soft',
        tone === 'warning' && 'border-warning/25 bg-warning-soft',
      )}
    >
      <div
        className={clsx(
          'shrink-0 [&>svg]:size-5',
          tone === 'accent' && 'text-accent-text',
          tone === 'success' && 'text-success',
          tone === 'warning' && 'text-warning',
        )}
      >
        {icon}
      </div>
      <div className="min-w-0 flex-1 text-[13.5px] text-fg">{children}</div>
      {actions && <div className="flex shrink-0 gap-2">{actions}</div>}
    </div>
  )
}
