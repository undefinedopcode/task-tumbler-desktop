import clsx from 'clsx'
import { CalendarDays, Loader2, Settings2 } from 'lucide-react'
import { useCallback, useEffect, useState } from 'react'
import { call, useSignal } from '../bridge/hooks'
import type { DeviceCalendars } from '../bridge/types'
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
