import clsx from 'clsx'
import { CornerDownLeft, Loader2, Sparkles } from 'lucide-react'
import { useCallback, useEffect, useRef, useState } from 'react'
import { call, useProp, useSignal } from '../bridge/hooks'
import type { AiState, CapturePreview } from '../bridge/types'
import { Chip, Kbd } from '../components/ui'
import { duration } from '../lib/format'
import { keys, usePlatform } from '../lib/platform'

// The quick-capture window (its own small window at #capture): one line to
// get a thought out of your head, with what it understood shown as you type.
export function CaptureView() {
  const ai = useProp<AiState>('app', 'ai', {})
  const platform = usePlatform()
  const [text, setText] = useState('')
  const [preview, setPreview] = useState<(CapturePreview & { aiError?: string }) | null>(null)
  const [asking, setAsking] = useState(false)
  const input = useRef<HTMLInputElement>(null)
  const textRef = useRef(text)
  textRef.current = text

  const reset = useCallback(() => {
    setText('')
    setPreview(null)
    setAsking(false)
    requestAnimationFrame(() => input.current?.focus())
  }, [])
  useEffect(() => {
    window.tumbler = { ...(window.tumbler ?? ({} as never)), resetCapture: reset }
    const onFocus = () => input.current?.focus()
    // Clicking away with nothing typed puts it away.
    const onBlur = () => !textRef.current.trim() && call('shell', 'hideWindow')
    window.addEventListener('focus', onFocus)
    window.addEventListener('blur', onBlur)
    return () => {
      window.removeEventListener('focus', onFocus)
      window.removeEventListener('blur', onBlur)
    }
  }, [reset])

  useEffect(() => {
    if (!text.trim()) return setPreview(null)
    if (preview?.fromAi) return
    let alive = true
    call<CapturePreview>('app', 'previewCapture', text).then((p) => alive && setPreview(p))
    return () => {
      alive = false
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [text])
  useSignal(
    'app',
    'aiCaptureReady',
    useCallback((t: string, p: CapturePreview) => {
      setAsking(false)
      if (t === textRef.current) setPreview(p)
    }, []),
  )
  useSignal(
    'app',
    'aiFailed',
    useCallback((feature: string, error: string) => {
      if (feature !== 'capture') return
      setAsking(false)
      setPreview((p) => ({ ...(p ?? { title: '', tags: [] }), aiError: error }))
    }, []),
  )

  const submit = async (value: string) => {
    if (!value.trim()) return
    if (await call<string>('app', 'capture', value)) {
      reset()
      call('shell', 'hideWindow')
    }
  }
  const askAi = () => {
    if (!ai.ready || !text.trim() || asking) return
    setAsking(true)
    call('app', 'aiCapture', text)
  }

  return (
    <div className="flex h-full flex-col bg-surface p-4">
      <div className="mb-2 flex items-center gap-2 text-[13px] font-medium text-muted">
        <span className="size-2 rounded-full bg-accent" /> What's on your mind?
      </div>
      <input
        ref={input}
        autoFocus
        value={text}
        onChange={(e) => {
          setText(e.target.value)
          if (preview?.fromAi) setPreview(null)
        }}
        onKeyDown={(e) => {
          if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) {
            e.preventDefault()
            askAi()
          } else if (e.key === 'Enter') submit(e.currentTarget.value)
          else if (e.key === 'Escape') {
            reset()
            call('shell', 'hideWindow')
          }
        }}
        placeholder="call dentist tomorrow at 3pm ~15m #phone"
        className="h-11 w-full rounded-xl border border-accent bg-surface px-3.5 text-[16px] text-fg ring-4 ring-accent-soft outline-none placeholder:text-subtle focus-visible:outline-none"
      />
      <div className="mt-2.5 flex min-h-6 flex-wrap items-center gap-1.5">
        {preview?.fromAi && (
          <Chip tone="success" icon={<Sparkles className="size-3" />}>
            Read by AI
          </Chip>
        )}
        {preview && (
          <>
            <span className="mr-1 max-w-[50%] truncate text-[13px] font-medium text-fg">{preview.title || '(no title yet)'}</span>
            {(preview.date || preview.time) && <Chip tone="accent">{[preview.date, preview.time].filter(Boolean).join(' ')}</Chip>}
            {preview.estimate ? <Chip>{duration(preview.estimate)}</Chip> : null}
            {preview.priority && preview.priority !== 'medium' && <Chip tone={preview.priority === 'low' ? 'neutral' : 'danger'}>{preview.priority}</Chip>}
            {preview.energy && <Chip>{preview.energy} energy</Chip>}
            {preview.recurrence && <Chip>{preview.recurrence}</Chip>}
            {preview.category && <Chip tone={preview.categoryKnown ? 'neutral' : 'warning'}>{preview.category}</Chip>}
            {preview.tags?.map((t) => (
              <Chip key={t}>#{t}</Chip>
            ))}
          </>
        )}
        {!preview && <span className="text-[12px] text-subtle">tomorrow · friday · at 3pm · ~30m · !high · +low · #tag · /category · every mon and thu</span>}
      </div>
      {preview?.aiError && <div className="mt-1 text-[12.5px] text-danger">AI couldn't read it: {preview.aiError}</div>}
      <div className="mt-auto flex items-center gap-3 text-[12px] text-subtle">
        {ai.ready && (
          <button
            onClick={askAi}
            disabled={!text.trim() || asking || !!preview?.fromAi}
            className={clsx('flex items-center gap-1.5 rounded-md px-1.5 py-1 font-medium text-accent-text hover:bg-accent-soft disabled:opacity-40')}
          >
            {asking ? <Loader2 className="size-3.5 animate-spin" /> : <Sparkles className="size-3.5" />}
            {asking ? 'Asking…' : 'Ask AI'} <Kbd>{keys(platform, 'Mod+↵')}</Kbd>
          </button>
        )}
        <span className="ml-auto flex items-center gap-1">
          <Kbd>
            <CornerDownLeft className="size-3" />
          </Kbd>
          add
        </span>
        <span className="flex items-center gap-1">
          <Kbd>Esc</Kbd> close
        </span>
      </div>
    </div>
  )
}
