import clsx from 'clsx'
import { BatteryCharging } from 'lucide-react'
import { call } from '../bridge/hooks'

const levels = [
  { level: 2, label: 'Wiped', bars: 1 },
  { level: 4, label: 'Low', bars: 2 },
  { level: 6, label: 'OK', bars: 3 },
  { level: 8, label: 'Good', bars: 4 },
  { level: 10, label: 'Great', bars: 5 },
]

// "How's your energy?" — five taps, no typing.
export function EnergyCheckIn() {
  return (
    <div className="flex items-center gap-4 rounded-xl border border-line bg-surface px-4 py-3 shadow-card">
      <BatteryCharging className="size-5 shrink-0 text-accent" />
      <div className="min-w-0 flex-1">
        <div className="text-[13.5px] font-medium text-fg">How's your energy right now?</div>
        <div className="text-[12.5px] text-muted">Plans fit around it: low energy keeps demanding work for another day.</div>
      </div>
      <div className="flex gap-1">
        {levels.map((l) => (
          <button
            key={l.level}
            onClick={() => call('app', 'logEnergy', l.level, '')}
            className="group flex w-14 flex-col items-center gap-1 rounded-lg py-1.5 text-[12px] font-medium text-muted transition-colors hover:bg-accent-soft hover:text-accent-text"
          >
            <span className="flex h-3.5 items-end gap-[2px]">
              {[1, 2, 3, 4, 5].map((b) => (
                <span
                  key={b}
                  className={clsx('w-[3px] rounded-sm', b <= l.bars ? 'bg-current' : 'bg-line-strong')}
                  style={{ height: `${4 + b * 2}px` }}
                />
              ))}
            </span>
            {l.label}
          </button>
        ))}
      </div>
    </div>
  )
}
