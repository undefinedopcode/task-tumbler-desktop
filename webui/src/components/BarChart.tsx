import clsx from 'clsx'
import { Tip } from './ui'

// Bars, with optional numbers above them and dots for a second measure
// (0–10, drawn on the same height). Hover a column for its tooltip.
export function BarChart({
  values,
  labels,
  topLabels,
  dots,
  dotMax = 10,
  tooltips,
  height = 160,
  barColor = (_: number) => 'var(--tt-accent)',
  everyOtherLabel,
}: {
  values: (number | null | undefined)[]
  labels: string[]
  topLabels?: string[]
  dots?: (number | null | undefined)[]
  dotMax?: number
  tooltips?: string[]
  height?: number
  barColor?: (v: number, i: number) => string
  everyOtherLabel?: boolean
}) {
  const max = Math.max(1, ...values.map((v) => v ?? 0))
  return (
    <div>
      <div className="relative flex items-end gap-[3px]" style={{ height }}>
        {[0.25, 0.5, 0.75, 1].map((f) => (
          <div key={f} className="pointer-events-none absolute inset-x-0 border-t border-dashed border-line" style={{ bottom: height * f }} />
        ))}
        {values.map((v, i) => {
          const h = v ? Math.max(2, (v / max) * (height - 18)) : 0
          const dot = dots?.[i]
          const column = (
            <div className="group relative flex h-full flex-1 flex-col items-center justify-end">
              {topLabels?.[i] && <span className="mb-0.5 text-[10.5px] font-semibold text-muted tabular-nums">{topLabels[i]}</span>}
              <div
                className="w-[62%] max-w-8 rounded-t-[4px] transition-opacity group-hover:opacity-80"
                style={{ height: h, background: v ? barColor(v, i) : 'transparent' }}
              />
              {dot !== undefined && dot !== null && (
                <span
                  className="absolute size-2.5 -translate-y-1/2 rounded-full border-2 border-surface bg-warning"
                  style={{ bottom: (dot / dotMax) * (height - 18) - 5 }}
                />
              )}
            </div>
          )
          return tooltips?.[i] ? (
            <Tip key={i} label={<span className="whitespace-pre-line">{tooltips[i]}</span>}>
              {column}
            </Tip>
          ) : (
            <div key={i} className="flex h-full flex-1">
              {column}
            </div>
          )
        })}
      </div>
      <div className="mt-1.5 flex gap-[3px]">
        {labels.map((l, i) => (
          <span key={i} className={clsx('flex-1 truncate text-center text-[10.5px] text-subtle', everyOtherLabel && i % 2 !== (labels.length - 1) % 2 && 'invisible')}>
            {l}
          </span>
        ))}
      </div>
    </div>
  )
}
