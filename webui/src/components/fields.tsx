import clsx from 'clsx'
import { useEffect, useRef, useState, type ReactNode } from 'react'

// Text that saves when you leave it (or press Enter, for one line). Follows
// the stored value when it changes elsewhere, unless you're mid-edit.
export function EditableText({
  value,
  onSave,
  placeholder,
  multiline,
  className,
  allowEmpty = true,
  'aria-label': ariaLabel,
}: {
  value: string
  onSave: (v: string) => void
  placeholder?: string
  multiline?: boolean
  className?: string
  allowEmpty?: boolean
  'aria-label'?: string
}) {
  const [text, setText] = useState(value)
  const editing = useRef(false)
  const ref = useRef<HTMLTextAreaElement>(null)
  useEffect(() => {
    if (!editing.current) setText(value)
  }, [value])
  // Grow with the text.
  useEffect(() => {
    const el = ref.current
    if (!el) return
    el.style.height = '0px'
    el.style.height = `${el.scrollHeight}px`
  }, [text])
  const commit = () => {
    editing.current = false
    const next = multiline ? text : text.trim()
    if (!allowEmpty && !next) {
      setText(value)
      return
    }
    if (next !== value) onSave(next)
  }
  return (
    <textarea
      ref={ref}
      aria-label={ariaLabel}
      value={text}
      rows={1}
      placeholder={placeholder}
      onFocus={() => (editing.current = true)}
      onChange={(e) => setText(multiline ? e.target.value : e.target.value.replace(/\n/g, ''))}
      onBlur={commit}
      onKeyDown={(e) => {
        if (e.key === 'Enter' && (!multiline || e.metaKey || e.ctrlKey)) {
          e.preventDefault()
          e.currentTarget.blur()
        }
        if (e.key === 'Escape') {
          editing.current = false
          setText(value)
          e.currentTarget.blur()
          e.stopPropagation()
        }
      }}
      className={clsx(
        'w-full resize-none overflow-hidden rounded-lg bg-transparent outline-none placeholder:text-subtle',
        'hover:bg-hover focus:bg-surface focus:ring-2 focus:ring-accent-soft focus-visible:outline-none',
        className,
      )}
    />
  )
}

// One row of a property list: a label on the left, the control on the right.
export function Field({ label, children, align = 'center' }: { label: string; children: ReactNode; align?: 'center' | 'start' }) {
  return (
    <div className={clsx('flex gap-4 py-1.5', align === 'center' ? 'items-center' : 'items-start')}>
      <div className={clsx('w-[92px] shrink-0 text-[13px] text-muted', align === 'start' && 'pt-1')}>{label}</div>
      <div className="min-w-0 flex-1">{children}</div>
    </div>
  )
}

export function PanelSection({ title, aside, children }: { title: string; aside?: ReactNode; children: ReactNode }) {
  return (
    <section className="border-t border-line px-6 py-4">
      <div className="mb-2 flex items-center gap-2">
        <h3 className="text-[12.5px] font-semibold tracking-wide text-muted uppercase">{title}</h3>
        {aside && <div className="ml-auto text-[12.5px] text-subtle">{aside}</div>}
      </div>
      {children}
    </section>
  )
}
