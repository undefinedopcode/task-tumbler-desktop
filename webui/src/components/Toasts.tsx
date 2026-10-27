import { X } from 'lucide-react'
import { useCallback, useState } from 'react'
import { call, useProp, useSignal } from '../bridge/hooks'

interface Toast {
  id: number
  text: string
  undo: boolean
}

let nextId = 1

// The controller's `message`s, bottom centre, with Undo when there's
// something to undo.
export function Toasts() {
  const [toasts, setToasts] = useState<Toast[]>([])
  const canUndo = useProp('app', 'canUndo', false)
  const dismiss = useCallback((id: number) => setToasts((t) => t.filter((x) => x.id !== id)), [])
  const onMessage = useCallback(
    (text: string) => {
      const id = nextId++
      setToasts((t) => [...t.slice(-2), { id, text, undo: true }])
      setTimeout(() => dismiss(id), 5000)
    },
    [dismiss],
  )
  useSignal('app', 'message', onMessage)

  return (
    <div className="pointer-events-none fixed inset-x-0 bottom-5 z-50 flex flex-col items-center gap-2">
      {toasts.map((t, i) => (
        <div
          key={t.id}
          className="pointer-events-auto flex animate-toast-in items-center gap-3 rounded-xl bg-toast py-2 pr-2 pl-4 text-[13.5px] text-toast-fg shadow-pop"
        >
          <span>{t.text}</span>
          {t.undo && canUndo && i === toasts.length - 1 && (
            <button
              className="rounded-md px-2 py-1 text-[13px] font-semibold text-toast-action hover:bg-white/10"
              onClick={() => {
                call('app', 'undo')
                dismiss(t.id)
              }}
            >
              Undo
            </button>
          )}
          <button aria-label="Dismiss" className="rounded-md p-1 opacity-60 hover:bg-white/10 hover:opacity-100" onClick={() => dismiss(t.id)}>
            <X className="size-3.5" />
          </button>
        </div>
      ))}
    </div>
  )
}
