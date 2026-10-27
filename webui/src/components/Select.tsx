import * as S from '@radix-ui/react-select'
import clsx from 'clsx'
import { Check, ChevronDown } from 'lucide-react'
import type { ReactNode } from 'react'

export interface Option {
  value: string
  label: ReactNode
  icon?: ReactNode
}

// Radix can't hold "" as a value; it stands in for "none".
const NONE = '\u0000none'

export function Select({
  value,
  options,
  onChange,
  placeholder = 'Choose…',
  className,
  size = 'md',
  'aria-label': ariaLabel,
}: {
  value: string
  options: Option[]
  onChange: (v: string) => void
  placeholder?: string
  className?: string
  size?: 'sm' | 'md'
  'aria-label'?: string
}) {
  return (
    // With no "" option, "" shows the placeholder.
    <S.Root
      value={value === '' ? (options.some((o) => o.value === '') ? NONE : undefined) : value}
      onValueChange={(v) => onChange(v === NONE ? '' : v)}
    >
      <S.Trigger
        aria-label={ariaLabel}
        className={clsx(
          'inline-flex items-center justify-between gap-2 rounded-lg border border-line bg-surface text-fg shadow-card outline-none hover:bg-raised',
          'data-placeholder:text-subtle focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft',
          size === 'sm' ? 'h-7 px-2 text-[13px]' : 'h-8 px-2.5 text-[13.5px]',
          className,
        )}
      >
        <span className="flex min-w-0 items-center gap-2 truncate">
          <S.Value placeholder={placeholder} />
        </span>
        <S.Icon>
          <ChevronDown className="size-3.5 text-subtle" />
        </S.Icon>
      </S.Trigger>
      <S.Portal>
        <S.Content
          position="popper"
          sideOffset={4}
          className="z-50 max-h-[min(360px,var(--radix-select-content-available-height))] min-w-[var(--radix-select-trigger-width)] animate-pop-in overflow-hidden rounded-xl border border-line bg-surface p-1 shadow-pop"
        >
          <S.Viewport>
            {options.map((o) => (
              <S.Item
                key={o.value || NONE}
                value={o.value === '' ? NONE : o.value}
                className="flex h-8 cursor-default items-center gap-2 rounded-lg pr-2 pl-2 text-[13px] text-fg outline-none data-highlighted:bg-hover"
              >
                {o.icon}
                <S.ItemText>{o.label}</S.ItemText>
                <S.ItemIndicator className="ml-auto">
                  <Check className="size-3.5 text-accent" />
                </S.ItemIndicator>
              </S.Item>
            ))}
          </S.Viewport>
        </S.Content>
      </S.Portal>
    </S.Root>
  )
}
