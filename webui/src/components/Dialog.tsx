import * as D from '@radix-ui/react-dialog'
import clsx from 'clsx'
import { X } from 'lucide-react'
import type { ReactNode } from 'react'

export function Dialog({
  open,
  onOpenChange,
  title,
  description,
  children,
  footer,
  width = 'md',
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  title: ReactNode
  description?: ReactNode
  children?: ReactNode
  footer?: ReactNode
  width?: 'sm' | 'md' | 'lg'
}) {
  return (
    <D.Root open={open} onOpenChange={onOpenChange}>
      <D.Portal>
        <D.Overlay className="fixed inset-0 z-40 bg-black/25 backdrop-blur-[2px] data-[state=open]:animate-[fade-in_120ms_ease-out]" />
        <D.Content
          className={clsx(
            'fixed top-1/2 left-1/2 z-50 flex max-h-[85vh] w-[calc(100vw-48px)] -translate-x-1/2 -translate-y-1/2 flex-col rounded-2xl border border-line bg-surface shadow-pop outline-none',
            'data-[state=open]:animate-pop-in',
            width === 'sm' && 'max-w-[400px]',
            width === 'md' && 'max-w-[520px]',
            width === 'lg' && 'max-w-[720px]',
          )}
        >
          <div className="flex items-start gap-3 px-5 pt-5 pb-3">
            <div className="min-w-0 flex-1">
              <D.Title className="text-[16px] font-semibold text-fg">{title}</D.Title>
              {description ? (
                <D.Description className="mt-1 text-[13px] text-muted">{description}</D.Description>
              ) : (
                <D.Description className="sr-only">{typeof title === 'string' ? title : ''}</D.Description>
              )}
            </div>
            <D.Close className="-mt-1 -mr-1 rounded-lg p-1.5 text-subtle hover:bg-hover hover:text-fg" aria-label="Close">
              <X className="size-4" />
            </D.Close>
          </div>
          {children && <div className="min-h-0 overflow-y-auto px-5 pb-4">{children}</div>}
          {footer && <div className="flex justify-end gap-2 border-t border-line px-5 py-3">{footer}</div>}
        </D.Content>
      </D.Portal>
    </D.Root>
  )
}

export function TextField({ className, ...props }: React.InputHTMLAttributes<HTMLInputElement>) {
  return (
    <input
      {...props}
      className={clsx(
        'h-8 w-full rounded-lg border border-line bg-surface px-2.5 text-[13.5px] text-fg shadow-card outline-none placeholder:text-subtle',
        'focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none',
        className,
      )}
    />
  )
}
