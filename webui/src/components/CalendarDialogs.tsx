import clsx from 'clsx'
import { CalendarDays, Loader2, Settings2 } from 'lucide-react'
import { useCallback, useEffect, useMemo, useState } from 'react'
import { call, useSignal } from '../bridge/hooks'
import type { DeviceCalendars, Feed } from '../bridge/types'
import { Dialog, TextField } from './Dialog'
import { Field } from './fields'
import { Select } from './Select'
import { Button, Dot, Toggle } from './ui'

const swatches = ['#64748b', '#6366f1', '#0ea5e9', '#10b981', '#f59e0b', '#ef4444', '#ec4899', '#a855f7']

// Subscribe to a calendar by its .ics address. The address is checked before
// anything is saved.
export function FeedDialog({ onClose }: { onClose: () => void }) {
  const [url, setUrl] = useState('')
  const [name, setName] = useState('')
  const [color, setColor] = useState(swatches[0])
  const [blocking, setBlocking] = useState('block_all')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState('')
  useSignal('sync', 'feedAdded', useCallback(() => onClose(), [onClose]))
  useSignal(
    'sync',
    'feedFailed',
    useCallback((e: string) => {
      setBusy(false)
      setError(e)
    }, []),
  )
  const subscribe = () => {
    if (!url.trim() || busy) return
    setBusy(true)
    setError('')
    call('sync', 'addFeed', { name: name.trim(), url: url.trim(), color, blocking })
  }
  return (
    <Dialog
      open
      onOpenChange={(o) => !o && onClose()}
      title="Subscribe to a calendar"
      description="Its events show on your schedule and keep tasks out of busy times. Read-only; refreshed every 15 minutes."
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button variant="primary" disabled={!url.trim() || busy} icon={busy ? <Loader2 className="size-4 animate-spin" /> : undefined} onClick={subscribe}>
            {busy ? 'Checking…' : 'Subscribe'}
          </Button>
        </>
      }
    >
      <TextField autoFocus value={url} onChange={(e) => setUrl(e.target.value)} onKeyDown={(e) => e.key === 'Enter' && subscribe()} placeholder="https://… .ics, webcal://… or a file path" />
      <p className="mt-2 text-[12.5px] leading-snug text-muted">
        Google Calendar: Settings → your calendar → “Secret address in iCal format”. Nextcloud: the calendar's ⋯ menu → “Copy subscription link”. Outlook: Settings → Shared calendars → Publish → ICS.
      </p>
      {error && <p className="mt-2 rounded-lg bg-danger-soft px-3 py-2 text-[13px] text-danger">{error}</p>}
      <div className="mt-3">
        <Field label="Name">
          <TextField value={name} onChange={(e) => setName(e.target.value)} placeholder="Work, Family, School holidays… (optional)" />
        </Field>
        <Field label="While on">
          <Select
            size="sm"
            value={blocking}
            onChange={setBlocking}
            options={[
              { value: 'block_all', label: 'Nothing else gets scheduled' },
              { value: 'recurring_only', label: 'Only routines' },
              { value: 'none', label: 'Anything can be scheduled' },
            ]}
          />
        </Field>
        <Field label="Colour">
          <div className="flex gap-1.5">
            {swatches.map((c) => (
              <button
                key={c}
                aria-label={c}
                onClick={() => setColor(c)}
                className={clsx('size-6 rounded-full transition-transform hover:scale-110', color === c && 'ring-2 ring-fg ring-offset-2 ring-offset-surface')}
                style={{ background: c }}
              />
            ))}
          </div>
        </Field>
      </div>
    </Dialog>
  )
}

// The Mac's own calendars (EventKit): ask once, then tick the ones to show.
export function DeviceCalendarsDialog({ onClose }: { onClose: () => void }) {
  const [d, setD] = useState<DeviceCalendars | null>(null)
  const refresh = useCallback(() => call<DeviceCalendars>('sync', 'deviceCalendars').then(setD), [])
  useEffect(() => {
    refresh()
    // Back from System Settings: the answer may have changed.
    window.addEventListener('focus', refresh)
    return () => window.removeEventListener('focus', refresh)
  }, [refresh])
  useSignal('sync', 'deviceCalendarsChanged', refresh)
  const groups = new Map<string, DeviceCalendars['calendars']>()
  for (const c of d?.calendars ?? []) groups.set(c.account, [...(groups.get(c.account) ?? []), c])

  return (
    <Dialog open onOpenChange={(o) => !o && onClose()} title="Calendars on this Mac" footer={<Button onClick={onClose}>Done</Button>}>
      {d?.access === 'notAsked' && (
        <div className="py-2">
          <p className="text-[13.5px] text-fg">Use the calendars you already have</p>
          <p className="mt-1 text-[13px] leading-snug text-muted">
            Events from iCloud, Google, Exchange or Outlook, and anything else in the Calendar app, show on your schedule and keep tasks out of busy times. They update as soon as they
            change. Task Tumbler only reads them. macOS will ask you once.
          </p>
          <Button variant="primary" className="mt-3" icon={<CalendarDays className="size-4" />} onClick={() => call('sync', 'requestCalendarAccess')}>
            Allow access…
          </Button>
        </div>
      )}
      {d?.access === 'denied' && (
        <div className="py-2">
          <p className="text-[13.5px] text-fg">Calendar access is off</p>
          <p className="mt-1 text-[13px] text-muted">Turn on Task Tumbler in System Settings → Privacy &amp; Security → Calendars, with full access.</p>
          <Button className="mt-3" icon={<Settings2 className="size-4" />} onClick={() => call('shell', 'openExternal', 'x-apple.systempreferences:com.apple.preference.security?Privacy_Calendars')}>
            Open System Settings
          </Button>
        </div>
      )}
      {d?.access === 'granted' && d.calendars.length === 0 && <p className="py-4 text-[13px] text-muted">No calendars on this Mac yet.</p>}
      {d?.access === 'granted' &&
        [...groups].map(([account, cals]) => (
          <div key={account} className="mb-3">
            <div className="mb-1 text-[12px] font-semibold tracking-wide text-subtle uppercase">{account}</div>
            {cals.map((c) => (
              <label key={c.id} className="flex h-9 items-center gap-3 rounded-lg px-1 hover:bg-hover">
                <Dot color={c.color} className="size-2.5" />
                <span className="flex-1 text-[13.5px] text-fg">{c.title}</span>
                <Toggle checked={!!c.feedId} onChange={(on) => (on ? call('sync', 'addDeviceCalendar', c.id) : call('sync', 'removeFeed', c.feedId))} />
              </label>
            ))}
          </div>
        ))}
    </Dialog>
  )
}

const here = Intl.DateTimeFormat().resolvedOptions().timeZone
const zones: string[] = (() => {
  try {
    return Intl.supportedValuesOf('timeZone')
  } catch {
    return []
  }
})()

function validZone(z: string): boolean {
  try {
    new Intl.DateTimeFormat(undefined, { timeZone: z })
    return true
  } catch {
    return false
  }
}

// The local day an all-day event on `d` in `zone` goes on: the one holding
// its midday there (as sync::dayIn does).
function dayHere(d: Date, zone: string): Date {
  const guess = Date.UTC(d.getFullYear(), d.getMonth(), d.getDate(), 12)
  const parts = Object.fromEntries(
    new Intl.DateTimeFormat('en-US', { timeZone: zone, hourCycle: 'h23', year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: 'numeric' })
      .formatToParts(guess)
      .map((p) => [p.type, Number(p.value)]),
  )
  const wall = Date.UTC(parts.year, parts.month - 1, parts.day, parts.hour, parts.minute)
  return new Date(guess - (wall - guess))
}

// Where a feed comes from, without the secret part of its address.
function source(url: string): string {
  try {
    const u = new URL(url.replace(/^webcals?:/, 'https:'))
    if (u.protocol === 'http:' || u.protocol === 'https:') return `From ${u.hostname}`
  } catch {
    // a file path
  }
  return 'A file on this computer'
}

const dayFmt = new Intl.DateTimeFormat(undefined, { weekday: 'short', day: 'numeric', month: 'short' })

// A subscribed calendar's settings: name, colour, how it blocks, and the
// time zone its all-day events' days belong to.
export function FeedSettingsDialog({ feed, onClose }: { feed: Feed; onClose: () => void }) {
  const [name, setName] = useState(feed.name)
  const [color, setColor] = useState(feed.color)
  const [blocking, setBlocking] = useState(feed.blocking || 'block_all')
  const [zone, setZone] = useState(feed.dayZone ?? '')
  const declared = feed.declaredZone && feed.declaredZone !== here && feed.declaredZone !== 'UTC' && validZone(feed.declaredZone) ? feed.declaredZone : ''
  const [mode, setMode] = useState(!zone ? 'here' : zone === declared ? 'declared' : 'other')
  const chosen = mode === 'here' ? '' : mode === 'declared' ? declared : zone.trim()
  const ok = !chosen || validZone(chosen)
  const example = useMemo(() => {
    if (!chosen || !ok) return ''
    const today = new Date()
    const there = dayHere(today, chosen)
    return `An all-day event on ${dayFmt.format(today)} there goes on ${dayFmt.format(there)} here.`
  }, [chosen, ok])
  const save = () => {
    if (!ok) return
    call('sync', 'updateFeed', feed.id, { name: name.trim() || feed.name, color, blocking, dayZone: chosen })
    onClose()
  }
  return (
    <Dialog
      open
      onOpenChange={(o) => !o && onClose()}
      title="Calendar settings"
      description={feed.device ? 'A calendar on this Mac.' : source(feed.url)}
      footer={
        <>
          <Button variant="ghost" onClick={onClose}>
            Cancel
          </Button>
          <Button variant="primary" disabled={!ok} onClick={save}>
            Save
          </Button>
        </>
      }
    >
      <Field label="Name">
        <TextField value={name} onChange={(e) => setName(e.target.value)} />
      </Field>
      <Field label="While on">
        <Select
          size="sm"
          value={blocking}
          onChange={setBlocking}
          options={[
            { value: 'block_all', label: 'Nothing else gets scheduled' },
            { value: 'recurring_only', label: 'Only routines' },
            ...(feed.blocking === 'tags_only' ? [{ value: 'tags_only', label: 'Only tasks with its tags' }] : []),
            { value: 'none', label: 'Anything can be scheduled' },
          ]}
        />
      </Field>
      <Field label="Colour">
        <div className="flex gap-1.5">
          {swatches.map((c) => (
            <button
              key={c}
              aria-label={c}
              onClick={() => setColor(c)}
              className={clsx('size-6 rounded-full transition-transform hover:scale-110', color === c && 'ring-2 ring-fg ring-offset-2 ring-offset-surface')}
              style={{ background: c }}
            />
          ))}
        </div>
      </Field>
      <Field label="All-day days">
        <div className="flex flex-col gap-2">
          <Select
            size="sm"
            value={mode}
            onChange={setMode}
            options={[
              { value: 'here', label: `As this computer's (${here})` },
              ...(declared ? [{ value: 'declared', label: `As the calendar's (${declared})` }] : []),
              { value: 'other', label: 'In another time zone…' },
            ]}
          />
          {mode === 'other' && (
            <>
              <TextField autoFocus list="tt-zones" value={zone} onChange={(e) => setZone(e.target.value)} placeholder="America/New_York" aria-label="Time zone" />
              <datalist id="tt-zones">
                {zones.map((z) => (
                  <option key={z} value={z} />
                ))}
              </datalist>
            </>
          )}
          <p className={clsx('text-[12.5px] leading-snug', ok ? 'text-muted' : 'text-danger')}>
            {!ok
              ? "That isn't a time zone Task Tumbler knows. Try a city name like America/New_York."
              : example ||
                'For a calendar kept in another country, US holidays say: its all-day events go on the day here that holds their midday there. Timed events already show at the right time.'}
          </p>
        </div>
      </Field>
    </Dialog>
  )
}
