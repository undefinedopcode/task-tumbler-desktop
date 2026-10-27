import * as DM from '@radix-ui/react-dropdown-menu'
import clsx from 'clsx'
import type { ReactNode } from 'react'

export const Menu = DM.Root
export const MenuTrigger = DM.Trigger

export function MenuContent({ children, align = 'end' }: { children: ReactNode; align?: 'start' | 'end' }) {
  return (
    <DM.Portal>
      <DM.Content
        align={align}
        sideOffset={4}
        className="z-50 min-w-48 animate-pop-in rounded-xl border border-line bg-surface p-1 shadow-pop"
      >
        {children}
      </DM.Content>
    </DM.Portal>
  )
}

export function MenuItem({
  children,
  icon,
  onSelect,
  danger,
  hint,
}: {
  children: ReactNode
  icon?: ReactNode
  onSelect: () => void
  danger?: boolean
  hint?: string
}) {
  return (
    <DM.Item
      onSelect={onSelect}
      className={clsx(
        'flex h-8 cursor-default items-center gap-2 rounded-lg px-2 text-[13px] outline-none',
        danger ? 'text-danger data-highlighted:bg-danger-soft' : 'text-fg data-highlighted:bg-hover',
      )}
    >
      <span className="flex size-4 items-center justify-center text-muted [&>svg]:size-4">{icon}</span>
      <span className="flex-1">{children}</span>
      {hint && <span className="text-[12px] text-subtle">{hint}</span>}
    </DM.Item>
  )
}

export function MenuSeparator() {
  return <DM.Separator className="mx-1 my-1 h-px bg-line" />
}

export function MenuLabel({ children }: { children: ReactNode }) {
  return <DM.Label className="px-2 pt-1.5 pb-1 text-[11.5px] font-semibold tracking-wide text-subtle uppercase">{children}</DM.Label>
}
