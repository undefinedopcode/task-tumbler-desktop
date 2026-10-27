import clsx from 'clsx'
import { AlertTriangle, CalendarCheck2, Loader2, RefreshCw, Sparkles } from 'lucide-react'
import { useCallback, useEffect, useRef, useState } from 'react'
import { call, useProp, useSignal } from '../bridge/hooks'
import type { AiState, EnergyState, Plan } from '../bridge/types'
import { duration, time } from '../lib/format'
import { isoDate } from './DatePicker'
import { Dialog } from './Dialog'
import { EnergyCheckIn } from './EnergyCheckIn'
import { Segmented } from './Segmented'
import { Button, Checkbox, Toggle } from './ui'

// "Plan my day": proposed blocks to review. Nothing is scheduled until you
// say so, and what you accept is one undoable step.
export function PlanDialog({ day: initialDay, onClose }: { day: string; onClose: () => void }) {
  const ai = useProp<AiState>('app', 'ai', {})
  const energy = useProp<EnergyState>('app', 'energy', {})
  const [day, setDay] = useState(initialDay)
  const [useAi, setUseAi] = useState(!!ai.ready)
  const [plan, setPlan] = useState<Plan | null>(null)
  const [waiting, setWaiting] = useState(true)
  const accepted = useRef(false)
  const latest = useRef<Plan | null>(null)
  latest.current = plan

  const today = isoDate(new Date())
  const tomorrow = isoDate(new Date(Date.now() + 86_400_000))

  const make = useCallback(
    (d: string, withAi: boolean) => {
      setWaiting(true)
      setPlan(null)
      call('app', 'planDay', d, withAi && !!ai.ready)
    },
    [ai.ready],
  )
  // Leaving without accepting tells the app (it learns from dismissed plans).
  useEffect(
    () => () => {
      if (latest.current && !accepted.current) call('app', 'dismissPlan', latest.current)
    },
    [],
  )
  useSignal(
    'app',
    'planReady',
    useCallback(
      (result: Plan) => {
        if (result.date !== day) return
        setPlan(result)
        setWaiting(false)
      },
      [day],
    ),
  )

  // After the planReady hook above, so the answer can't arrive first.
  useEffect(() => {
    make(day, useAi)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // A check-in from the card above changes the plan: make it again.
  const lastCheckIn = useRef(energy.at)
  useEffect(() => {
    if (energy.at !== lastCheckIn.current) {
      lastCheckIn.current = energy.at
      make(day, useAi)
    }
  }, [energy.at, day, useAi, make])

  const chosen = plan?.blocks.filter((b) => b.checked) ?? []
  const accept = async () => {
    if (!plan) return
    if (await call<boolean>('app', 'acceptPlan', chosen, plan)) {
      accepted.current = true
      onClose()
    }
  }
  const paced = plan?.blocks.some((b) => b.estimate > 0 && b.end - b.start > b.estimate * 60_000)

  return (
    <Dialog
      open
      onOpenChange={(o) => !o && onClose()}
      width="lg"
      title={
        <span className="flex items-center gap-2">
          <Sparkles className="size-5 text-accent" /> Plan {day === today ? 'my day' : day === tomorrow ? 'tomorrow' : plan?.label ?? 'the day'}
        </span>
      }
      description={
        energy.level !== undefined && day === today
          ? `Fitted around your category hours, what's already scheduled, and your energy (${energy.label}).`
          : "Fitted around your category hours and what's already scheduled. Untick anything you don't want."
      }
      footer={
        plan && plan.blocks.length > 0 ? (
          <>
            <span className="mr-auto self-center text-[12.5px] text-muted">
              {chosen.length} of {plan.blocks.length} selected
            </span>
            <Button variant="ghost" onClick={onClose}>
              Not now
            </Button>
            <Button variant="primary" icon={<CalendarCheck2 className="size-4" />} disabled={!chosen.length} onClick={accept}>
              Add {chosen.length} {chosen.length === 1 ? 'block' : 'blocks'}
            </Button>
          </>
        ) : undefined
      }
    >
      <div className="mb-4 flex flex-wrap items-center gap-3">
        <Segmented
          value={day === today ? 'today' : day === tomorrow ? 'tomorrow' : 'other'}
          onChange={(v) => {
            const d = v === 'today' ? today : tomorrow
            setDay(d)
            make(d, useAi)
          }}
          options={[
            { value: 'today', label: 'Today' },
            { value: 'tomorrow', label: 'Tomorrow' },
            ...(day !== today && day !== tomorrow ? [{ value: 'other' as const, label: plan?.label ?? day }] : []),
          ]}
        />
        {ai.provider && ai.provider !== 'off' && (
          <label className={clsx('flex items-center gap-2 text-[13px]', ai.ready ? 'text-fg' : 'text-subtle')}>
            <Toggle
              checked={useAi && !!ai.ready}
              onChange={(v) => {
                setUseAi(v)
                make(day, v)
              }}
            />
            {ai.ready ? `Ask ${ai.label}` : 'AI not set up'}
          </label>
        )}
        <Button variant="ghost" size="sm" className="ml-auto" disabled={waiting} icon={<RefreshCw className="size-3.5" />} onClick={() => make(day, useAi)}>
          Plan again
        </Button>
      </div>

      {day === today && energy.due && !waiting && (
        <div className="mb-4">
          <EnergyCheckIn />
        </div>
      )}

      {waiting && (
        <div className="flex items-center justify-center gap-3 py-12 text-[13.5px] text-muted">
          <Loader2 className="size-5 animate-spin text-accent" />
          {useAi && ai.ready ? `Asking ${ai.label}…` : 'Working it out…'}
        </div>
      )}

      {plan && (
        <>
          {plan.aiError && (
            <div className="mb-3 flex items-start gap-2 rounded-lg bg-warning-soft px-3 py-2 text-[13px] text-fg">
              <AlertTriangle className="mt-0.5 size-4 shrink-0 text-warning" />
              The AI couldn't help this time ({plan.aiError}), so this is the offline plan.
            </div>
          )}
          {plan.explanation && (
            <div className="mb-3 rounded-lg bg-accent-soft px-3 py-2 text-[13px] text-fg">
              {plan.explanation}
              {plan.by && <span className="ml-1 text-subtle">({plan.by})</span>}
            </div>
          )}
          {plan.blocks.length === 0 ? (
            <div className="py-10 text-center">
              <div className="text-[14px] font-medium text-fg">Nothing to add</div>
              <div className="mt-1 text-[13px] text-muted">
                {plan.unplaced.length
                  ? "Everything open is either waiting, saved for later, or doesn't fit the day's hours."
                  : 'Every open task already has a time, or there’s nothing open.'}
              </div>
            </div>
          ) : (
            <div className="rounded-xl border border-line p-1">
              {plan.blocks.map((b, i) => {
                const minutes = Math.round((b.end - b.start) / 60_000)
                return (
                  <label key={`${b.taskId}-${b.start}`} className="flex cursor-default items-start gap-3 rounded-lg px-3 py-2 hover:bg-hover">
                    <span className="pt-0.5">
                      <Checkbox
                        checked={b.checked}
                        onChange={(c) => setPlan({ ...plan, blocks: plan.blocks.map((x, j) => (j === i ? { ...x, checked: c } : x)) })}
                      />
                    </span>
                    <span className="w-[92px] shrink-0">
                      <span className="block text-[13px] font-medium text-fg tabular-nums">{time(b.start)}</span>
                      <span className="block text-[11.5px] text-subtle">
                        {duration(minutes)}
                        {b.estimate > 0 && minutes > b.estimate && ` (est. ${duration(b.estimate)})`}
                      </span>
                    </span>
                    <span className={clsx('min-w-0 flex-1', !b.checked && 'opacity-50')}>
                      <span className="block truncate text-[13.5px] text-fg">{b.title}</span>
                      {b.reason && <span className="block truncate text-[12px] text-muted">{b.reason}</span>}
                    </span>
                  </label>
                )
              })}
            </div>
          )}
          {paced && (
            <p className="mt-2 text-[12px] text-subtle">Blocks marked “est.” are longer than their estimate, to allow for how long things like them usually take you.</p>
          )}
          {plan.unplaced.length > 0 && (
            <div className="mt-4">
              <div className="mb-1 text-[12px] font-semibold tracking-wide text-muted uppercase">Not in this plan · {plan.unplaced.length}</div>
              {plan.unplaced.map((u) => (
                <div key={u.taskId} className="flex gap-3 py-1 text-[13px]">
                  <span className="w-[45%] truncate text-fg">{u.title}</span>
                  <span className="flex-1 truncate text-muted">{u.reason}</span>
                </div>
              ))}
            </div>
          )}
        </>
      )}
    </Dialog>
  )
}
