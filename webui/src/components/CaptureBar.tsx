import clsx from 'clsx'
import { CalendarClock, Clock, CornerDownLeft, Flag, Plus, Repeat, Tag, Zap } from 'lucide-react'
import { forwardRef, useEffect, useState } from 'react'
import { call } from '../bridge/hooks'
import type { CapturePreview } from '../bridge/types'
import { duration } from '../lib/format'
import { keys, usePlatform } from '../lib/platform'
import { Chip, Kbd } from './ui'

// One line to add a task, with what it understood shown as you type
// ("call mum tomorrow 3pm #home 15m").
export const CaptureBar = forwardRef<HTMLInputElement>(function CaptureBar(_, ref) {
  const [text, setText] = useState('')
  const [preview, setPreview] = useState<CapturePreview | null>(null)
  const [focused, setFocused] = useState(false)
  const platform = usePlatform()

  useEffect(() => {
    if (!text.trim()) {
      setPreview(null)
      return
    }
    let alive = true
    const t = setTimeout(() => call<CapturePreview>('app', 'previewCapture', text).then((p) => alive && setPreview(p)), 80)
    return () => {
      alive = false
      clearTimeout(t)
    }
  }, [text])

  const submit = async () => {
    if (!text.trim()) return
    const id = await call<string>('app', 'capture', text)
    if (id) setText('')
  }

  const chips = preview ? previewChips(preview) : []
  return (
    <div
      className={clsx(
        'rounded-xl border bg-surface shadow-card transition-[border-color,box-shadow]',
        focused ? 'border-accent ring-4 ring-accent-soft' : 'border-line',
      )}
    >
      <div className="flex h-11 items-center gap-2.5 px-3.5">
        <Plus className={clsx('size-[18px] shrink-0', focused ? 'text-accent' : 'text-subtle')} />
        <input
          ref={ref}
          value={text}
          onChange={(e) => setText(e.target.value)}
          onFocus={() => setFocused(true)}
          onBlur={() => setFocused(false)}
          onKeyDown={(e) => {
            if (e.key === 'Enter') submit()
            if (e.key === 'Escape') {
              setText('')
              e.currentTarget.blur()
            }
          }}
          placeholder="Add a task — try “call the dentist tomorrow at 10am ~15m #health”"
          className="h-full flex-1 bg-transparent text-[14.5px] text-fg outline-none placeholder:text-subtle focus-visible:outline-none"
          spellCheck
        />
        {text ? (
          <span className="flex items-center gap-1 text-[12px] text-subtle">
            <Kbd>
              <CornerDownLeft className="size-3" />
            </Kbd>
            to add
          </span>
        ) : (
          <Kbd>{keys(platform, 'Mod+N')}</Kbd>
        )}
      </div>
      {chips.length > 0 && (
        <div className="flex flex-wrap items-center gap-1.5 border-t border-line px-3.5 py-2">
          <span className="mr-1 max-w-[50%] truncate text-[12.5px] font-medium text-fg">{preview?.title || 'Untitled'}</span>
          {chips}
        </div>
      )}
    </div>
  )
})

function previewChips(p: CapturePreview) {
  const out = []
  if (p.date || p.time)
    out.push(
      <Chip key="when" tone="accent" icon={<CalendarClock className="size-3" />}>
        {[p.date, p.time].filter(Boolean).join(' ')}
      </Chip>,
    )
  if (p.estimate)
    out.push(
      <Chip key="est" icon={<Clock className="size-3" />}>
        {duration(p.estimate)}
      </Chip>,
    )
  if (p.priority && p.priority !== 'medium')
    out.push(
      <Chip key="pri" tone={p.priority === 'urgent' || p.priority === 'high' ? 'danger' : 'neutral'} icon={<Flag className="size-3" />}>
        {p.priority}
      </Chip>,
    )
  if (p.energy)
    out.push(
      <Chip key="energy" icon={<Zap className="size-3" />}>
        {p.energy} energy
      </Chip>,
    )
  if (p.recurrence)
    out.push(
      <Chip key="rec" icon={<Repeat className="size-3" />}>
        {p.recurrence}
      </Chip>,
    )
  if (p.category)
    out.push(
      <Chip key="cat" tone={p.categoryKnown ? 'neutral' : 'warning'}>
        {p.category}
        {p.categoryKnown ? '' : ' (new?)'}
      </Chip>,
    )
  for (const t of p.tags ?? [])
    out.push(
      <Chip key={`tag-${t}`} icon={<Tag className="size-3" />}>
        {t}
      </Chip>,
    )
  return out
}
