import { Save } from 'lucide-react'
import { useEffect, useState } from 'react'
import { call, useProp, useQuery } from '../bridge/hooks'
import type { EnergyState } from '../bridge/types'
import { BarChart } from '../components/BarChart'
import { EnergyCheckIn } from '../components/EnergyCheckIn'
import { Row, Section } from '../components/settings'
import { Button } from '../components/ui'
import { time } from '../lib/format'

interface History {
  energy: { at: number; level: number; label?: string }[]
  days: { date: string; label: string; energy?: number | null; sleep?: number | null; readiness?: number | null; hours?: number | null }[]
}
interface Health {
  sleep?: number | null
  readiness?: number | null
  hours?: number | null
}

// Energy check-ins and last night's sleep, with two weeks of history.
export function EnergyPage() {
  const energy = useProp<EnergyState>('app', 'energy', {})
  const history = useQuery<History>('app', 'energyHistory', [14], { energy: [], days: [] })
  const health = useQuery<Health>('app', 'healthToday', [], {})
  const [sleep, setSleep] = useState('')
  const [readiness, setReadiness] = useState('')
  const [hours, setHours] = useState('')
  useEffect(() => {
    setSleep(health.sleep != null ? String(health.sleep) : '')
    setReadiness(health.readiness != null ? String(health.readiness) : '')
    setHours(health.hours != null ? String(health.hours) : '')
  }, [health])
  const anything = history.days.some((d) => d.energy != null || d.sleep != null)
  const field = (value: string, set: (v: string) => void, placeholder: string) => (
    <input
      value={value}
      onChange={(e) => set(e.target.value.replace(/[^0-9.]/g, ''))}
      placeholder={placeholder}
      className="h-8 w-24 rounded-lg border border-line bg-surface px-2.5 text-right text-[13.5px] text-fg tabular-nums shadow-card outline-none placeholder:text-subtle focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none"
    />
  )

  return (
    <div className="mx-auto flex w-full max-w-[820px] flex-col gap-8 px-8 pt-8 pb-16">
      <header>
        <h1 className="text-[26px] leading-tight font-semibold tracking-tight">Energy</h1>
        <p className="mt-1 text-[13.5px] text-muted">
          {energy.level !== undefined && energy.at ? `Last check-in: ${energy.label} at ${time(energy.at)}` : 'No check-in yet today'}
        </p>
      </header>
      <EnergyCheckIn />

      <Section id="trend" title="The last two weeks">
        <div className="p-4">
          {anything ? (
            <>
              <BarChart
                values={history.days.map((d) => d.energy)}
                labels={history.days.map((d) => d.label)}
                dots={history.days.map((d) => (d.sleep != null ? d.sleep / 10 : null))}
                barColor={(v) => (v <= 4 ? 'var(--tt-warning)' : 'var(--tt-success)')}
                tooltips={history.days.map((d) =>
                  [d.label, d.energy != null ? `Energy ${d.energy.toFixed(1)} of 10` : null, d.sleep != null ? `Sleep score ${d.sleep}` : null].filter(Boolean).join('\n'),
                )}
                everyOtherLabel
              />
              <div className="mt-3 flex gap-4 text-[12px] text-muted">
                <span className="flex items-center gap-1.5">
                  <span className="size-2.5 rounded-sm bg-success" /> Energy (average of the day's check-ins)
                </span>
                <span className="flex items-center gap-1.5">
                  <span className="size-2.5 rounded-full bg-warning" /> Sleep score
                </span>
              </div>
            </>
          ) : (
            <p className="py-8 text-center text-[13px] text-muted">Check in a few times a day and this fills in, so you can spot patterns.</p>
          )}
        </div>
      </Section>

      <Section id="sleep" title="Last night" description="From your watch or ring, if you have one. All optional.">
        <Row label="Sleep score" description="0–100">
          {field(sleep, setSleep, '—')}
        </Row>
        <Row label="Readiness score" description="0–100">
          {field(readiness, setReadiness, '—')}
        </Row>
        <Row label="Hours slept">{field(hours, setHours, '—')}</Row>
        <div className="flex justify-end px-4 py-3">
          <Button variant="primary" size="sm" icon={<Save className="size-3.5" />} onClick={() => call('app', 'saveHealth', { sleep, readiness, hours })}>
            Save
          </Button>
        </div>
      </Section>

      {history.energy.length > 0 && (
        <Section id="checkins" title="Check-ins">
          {[...history.energy]
            .reverse()
            .slice(0, 12)
            .map((e) => (
              <Row
                key={e.at}
                label={`${e.label ?? ''} (${e.level}/10)`}
                description={new Date(e.at).toLocaleString(undefined, { weekday: 'short', day: 'numeric', month: 'short', hour: 'numeric', minute: '2-digit' })}
              />
            ))}
        </Section>
      )}
    </div>
  )
}
