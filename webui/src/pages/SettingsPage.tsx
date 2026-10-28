import clsx from 'clsx'
import qrcode from 'qrcode-generator'
import { CalendarDays, Copy, FolderOpen, Link2, Loader2, Plug, RefreshCw, Save, Send, Trash2, Upload, Wifi, X } from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import { call, useProp, useQuery } from '../bridge/hooks'
import type { AiSettings, DeviceCalendars, RemoteState, Settings, SyncState } from '../bridge/types'
import { DeviceCalendarsDialog, FeedDialog } from '../components/CalendarDialogs'
import { Segmented } from '../components/Segmented'
import { Select } from '../components/Select'
import { Code, Row, Section, Stepper, TextSetting } from '../components/settings'
import { TimePicker } from '../components/TimePicker'
import { Button, Checkbox, Dot, IconButton, Toggle } from '../components/ui'
import { usePlatform } from '../lib/platform'

const sections = [
  { id: 'general', label: 'General' },
  { id: 'planning', label: 'Planning' },
  { id: 'ai', label: 'AI' },
  { id: 'calendars', label: 'Calendars' },
  { id: 'linear', label: 'Linear' },
  { id: 'phone', label: 'Phone' },
  { id: 'apps', label: 'Other apps' },
  { id: 'data', label: 'Your data' },
]

const set = (key: string, value: unknown) => call('app', 'setSetting', key, value)

export function SettingsPage() {
  const s = useProp<Settings>('app', 'settings', {})
  const platform = usePlatform()
  const mac = platform === 'mac'
  const scroller = useRef<HTMLDivElement>(null)
  const [active, setActive] = useState('general')

  // Highlight the section in view.
  useEffect(() => {
    const el = scroller.current
    if (!el) return
    const onScroll = () => {
      const top = el.getBoundingClientRect().top
      let current = sections[0].id
      for (const sec of sections) {
        const node = document.getElementById(sec.id)
        if (node && node.getBoundingClientRect().top - top < 120) current = sec.id
      }
      setActive(current)
    }
    el.addEventListener('scroll', onScroll)
    return () => el.removeEventListener('scroll', onScroll)
  }, [])

  return (
    <div className="flex h-full">
      <nav className="w-[184px] shrink-0 px-4 pt-8">
        <h1 className="mb-4 px-2 text-[22px] font-semibold tracking-tight">Settings</h1>
        {sections.map((sec) => (
          <button
            key={sec.id}
            onClick={() => document.getElementById(sec.id)?.scrollIntoView({ behavior: 'smooth' })}
            className={clsx(
              'flex h-8 w-full items-center rounded-lg px-2 text-left text-[13.5px] font-medium transition-colors',
              active === sec.id ? 'bg-hover text-fg' : 'text-muted hover:text-fg',
            )}
          >
            {sec.label}
          </button>
        ))}
      </nav>
      <div ref={scroller} className="min-w-0 flex-1 overflow-y-auto">
        <div className="mx-auto flex max-w-[720px] flex-col gap-8 px-8 pt-8 pb-24">
          <Section id="general" title="General">
            <Row label="Appearance" description="One look everywhere; light or dark follows your system unless you pick one.">
              <Segmented
                size="sm"
                value={(s.appearance as string) ?? 'system'}
                onChange={(v) => set('appearance', v)}
                options={[
                  { value: 'system', label: 'System' },
                  { value: 'light', label: 'Light' },
                  { value: 'dark', label: 'Dark' },
                ]}
              />
            </Row>
            <Row label={mac ? 'Keep running in the menu bar when closed' : 'Keep running in the system tray when closed'} description="Reminders only work while Task Tumbler is running.">
              <Toggle checked={!!s.closeToTray} onChange={(v) => set('close_to_tray', v)} />
            </Row>
            <Row label="Start at login" description={mac ? 'Starts quietly in the menu bar.' : 'Starts quietly in the tray.'}>
              <Toggle checked={!!s.autostart} onChange={(v) => set('autostart', v)} />
            </Row>
            <Row label="Desktop notifications" description="Before and at the start of each block, with buttons to start, bump or finish it.">
              <Toggle checked={!!s.notifications} onChange={(v) => set('notifications', v)} />
            </Row>
            <Row label="Heads-up before a block" description="0 for none.">
              <Stepper value={Number(s.reminderMinutes ?? 5)} min={0} max={60} suffix="min" onChange={(v) => set('reminder_minutes', v)} />
            </Row>
            <Row label="Ask about time away after" description="Minutes idle while working. 0 to never ask.">
              <Stepper value={Number(s.idleMinutes ?? 10)} min={0} max={120} step={5} suffix="min" onChange={(v) => set('idle_minutes', v)} />
            </Row>
            <Row
              label="Quick capture from anywhere"
              description={mac ? undefined : 'Change it in System Settings → Shortcuts → Task Tumbler.'}
            >
              <Code>{mac ? '⇧⌘Space' : 'Meta+Shift+Space'}</Code>
            </Row>
          </Section>

          <Section id="planning" title="Planning" description="How plans are fitted around you.">
            <Row label="Your best hours" description="Demanding work goes here when it can.">
              <div className="flex items-center gap-2">
                <TimePicker value={String(s.peakEnergyStart ?? '09:00')} onChange={(v) => v && set('peak_energy_start', v)} />
                <span className="text-[13px] text-subtle">to</span>
                <TimePicker value={String(s.peakEnergyEnd ?? '12:00')} onChange={(v) => v && set('peak_energy_end', v)} />
              </div>
            </Row>
            <Row label="Break after each planned block">
              <Stepper value={Number(s.breakMinutes ?? 15)} min={0} max={60} step={5} suffix="min" onChange={(v) => set('break_duration_minutes', v)} />
            </Row>
            <Row
              label="Plan with your real pace"
              description="Once you've finished a few timed tasks, new blocks allow for how long things like them usually take you, up to double the estimate. Never shorter."
            >
              <Toggle checked={!!s.planWithPace} onChange={(v) => set('plan_with_pace', v)} />
            </Row>
            <Row label="Nudge me to close out the day" description="A note on Today (and a notification) from this time until you've closed out.">
              <div className="flex items-center gap-2">
                {s.closeOutTime ? <TimePicker value={String(s.closeOutTime)} onChange={(v) => v && set('closeout_time', v)} /> : null}
                <Toggle checked={!!s.closeOutTime} onChange={(v) => set('closeout_time', v ? '17:30' : '')} />
              </div>
            </Row>
            <Row label="Weekly review" description="Shown on Today from this day until you've done it.">
              <Select
                size="sm"
                value={String(s.reviewWeekday ?? 0)}
                onChange={(v) => set('review_weekday', Number(v))}
                className="min-w-[140px]"
                options={[
                  { value: '-1', label: 'Off' },
                  ...[0, 1, 2, 3, 4, 5, 6].map((d) => ({ value: String(d), label: new Date(2024, 0, 7 + d).toLocaleDateString(undefined, { weekday: 'long' }) })),
                ]}
              />
            </Row>
            <Row label="Notes for the AI planner" description="Anything that helps it plan for you, e.g. “admin goes after lunch”." stacked>
              <HintsField value={String(s.aiHints ?? '')} />
            </Row>
          </Section>

          <AiSection />
          <CalendarsSection />
          <LinearSection />
          <PhoneSection />

          <Section id="apps" title="Other apps">
            <Row
              label="Let other apps change tasks"
              description={`${mac ? '' : 'Scripts, KRunner and '}AI agents (through MCP) can add, finish and schedule tasks. Reading is always allowed. Changes can be undone here.`}
            >
              <Toggle checked={!!s.externalWrites} onChange={(v) => set('external_writes', v)} />
            </Row>
            {!mac && (
              <Row label="Show in KRunner" description="Alt+Space, then “tt call mum tomorrow” to capture, or type a task's name to start or finish it.">
                <Toggle checked={!!s.krunner} onChange={(v) => set('krunner', v)} />
              </Row>
            )}
            <Row label="AI agents (MCP)" description="Add a stdio MCP server that runs task-tumbler --mcp. For Claude Code:" stacked>
              <CopyLine text="claude mcp add task-tumbler -- task-tumbler --mcp" />
            </Row>
            <Row label="From a terminal" stacked>
              <CopyLine text={'task-tumbler --add "call mum tomorrow at 6pm"'} />
            </Row>
          </Section>

          <DataSection />
        </div>
      </div>
    </div>
  )
}

function HintsField({ value }: { value: string }) {
  const [text, setText] = useState(value)
  useEffect(() => setText(value), [value])
  return (
    <textarea
      value={text}
      rows={3}
      onChange={(e) => setText(e.target.value)}
      onBlur={() => text !== value && set('ai_scheduling_hints', text)}
      className="w-full resize-y rounded-lg border border-line bg-surface px-2.5 py-2 text-[13.5px] text-fg shadow-card outline-none focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none"
    />
  )
}

function CopyLine({ text, onCopy }: { text: string; onCopy?: () => void }) {
  const [copied, setCopied] = useState(false)
  return (
    <div className="flex items-center gap-2 rounded-lg border border-line bg-raised py-1 pr-1 pl-3">
      <code className="min-w-0 flex-1 truncate font-mono text-[12.5px] text-fg select-text">{text}</code>
      <Button
        size="sm"
        variant="ghost"
        icon={<Copy className="size-3.5" />}
        onClick={() => {
          if (onCopy) onCopy()
          else navigator.clipboard?.writeText(text)
          setCopied(true)
          setTimeout(() => setCopied(false), 1500)
        }}
      >
        {copied ? 'Copied' : 'Copy'}
      </Button>
    </div>
  )
}

function AiSection() {
  const a = useProp<AiSettings>('app', 'ai', {})
  const models = useProp<string[]>('app', 'ollamaModels', [])
  const [key, setKey] = useState('')
  useEffect(() => {
    if (a.provider === 'ollama') call('app', 'refreshOllamaModels')
  }, [a.provider])
  const saveKey = () => {
    if (!key.trim()) return
    call('app', 'setApiKey', key.trim())
    setKey('')
  }
  return (
    <Section id="ai" title="AI" description="Everything works without AI. With it, plans and steps get smarter.">
      <Row label="Use">
        <Select
          size="sm"
          value={a.provider ?? 'off'}
          onChange={(v) => set('ai_provider', v)}
          className="min-w-[220px]"
          options={[
            { value: 'off', label: 'No AI' },
            { value: 'anthropic', label: 'Claude (Anthropic API key)' },
            { value: 'claude_cli', label: 'Claude Code (signed-in CLI)' },
            { value: 'ollama', label: 'A local model (Ollama)' },
          ]}
        />
      </Row>
      {a.provider === 'anthropic' && (
        <>
          <Row label="API key" description={a.hasKey ? 'Saved in your keyring. Type a new one to replace it.' : 'From console.anthropic.com → API keys. Kept in your keyring.'} stacked>
            <div className="flex gap-2">
              <input
                type="password"
                value={key}
                onChange={(e) => setKey(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && saveKey()}
                placeholder={a.hasKey ? '••••••••••••' : 'sk-ant-…'}
                className="h-8 flex-1 rounded-lg border border-line bg-surface px-2.5 font-mono text-[13px] text-fg shadow-card outline-none placeholder:text-subtle focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none"
              />
              <Button disabled={!key.trim()} icon={<Save className="size-4" />} onClick={saveKey}>
                Save key
              </Button>
            </div>
          </Row>
          <Row label="Model">
            <TextSetting value={a.model ?? ''} onSave={(v) => v && set('ai_model', v)} mono className="w-[240px]" />
          </Row>
        </>
      )}
      {a.provider === 'claude_cli' && (
        <>
          <Row
            label="Claude Code"
            description={
              a.cliPath ? (
                <>
                  Found at <Code>{a.cliPath}</Code>. It answers as whoever is signed in to it; nothing else to set up.
                </>
              ) : (
                <>
                  Not found. Install Claude Code and sign in with <Code>claude</Code>, or give the path to it.
                </>
              )
            }
            stacked
          >
            <TextSetting value={a.cliConfiguredPath ?? ''} placeholder="Found automatically" onSave={(v) => set('claude_cli_path', v)} mono />
          </Row>
          <Row label="Model" description="Empty for its default, or e.g. sonnet, opus, haiku.">
            <TextSetting value={a.cliModel ?? ''} placeholder="Its default" onSave={(v) => set('claude_cli_model', v)} mono className="w-[240px]" />
          </Row>
        </>
      )}
      {a.provider === 'ollama' && (
        <>
          <Row label="Ollama address">
            <TextSetting
              value={a.ollamaUrl ?? ''}
              onSave={(v) => {
                if (!v) return
                set('ollama_url', v)
                call('app', 'refreshOllamaModels')
              }}
              mono
              className="w-[260px]"
            />
          </Row>
          <Row label="Model" description={models.length ? undefined : 'No models found yet. Is Ollama running?'}>
            <div className="flex items-center gap-1">
              <Select size="sm" value={a.ollamaModel ?? ''} placeholder="Choose a model" onChange={(v) => set('ollama_model', v)} className="min-w-[200px]" options={models.map((m) => ({ value: m, label: m }))} />
              <IconButton label="Look again" size="sm" onClick={() => call('app', 'refreshOllamaModels')}>
                <RefreshCw className="size-3.5" />
              </IconButton>
            </div>
          </Row>
        </>
      )}
      {(a.provider === 'anthropic' || a.provider === 'claude_cli' || (a.provider === 'ollama' && a.ollamaCloud)) && (
        <Row
          label="Include task details"
          description={a.provider === 'ollama' ? 'This is an Ollama cloud model, so it runs off this computer. Off: only titles, times and priorities are sent.' : 'Off: only titles, times and priorities leave this computer.'}
        >
          <Toggle checked={!!a.shareNotes} onChange={(v) => set('ai_share_notes', v)} />
        </Row>
      )}
      {a.provider && a.provider !== 'off' && (
        <Row label="Connection" description={a.ready ? `Ready: ${a.label}` : 'Not set up yet.'}>
          <Button size="sm" disabled={!a.ready || a.busy} icon={a.busy ? <Loader2 className="size-3.5 animate-spin" /> : <Plug className="size-3.5" />} onClick={() => call('app', 'testAi')}>
            {a.busy ? 'Checking…' : 'Test connection'}
          </Button>
        </Row>
      )}
    </Section>
  )
}

function CalendarsSection() {
  const st = useProp<SyncState>('sync', 'state', {})
  const device = useQuery<DeviceCalendars | null>('sync', 'deviceCalendars', [], null, ['deviceCalendarsChanged'])
  const [dialog, setDialog] = useState<'feed' | 'device' | null>(null)
  const feeds = st.feeds ?? []
  return (
    <Section id="calendars" title="Calendars" description="Events show on your schedule and keep tasks out of busy times. Task Tumbler only reads them.">
      {feeds.map((f) => (
        <Row
          key={f.id}
          label={
            <span className="flex items-center gap-2">
              <Dot color={f.color} className="size-2.5" />
              {f.name}
              {f.device && <span className="text-[11.5px] font-normal text-subtle">on this Mac</span>}
            </span>
          }
          description={
            f.error ? (
              <span className="text-danger">{f.error}</span>
            ) : f.lastSync ? (
              `${f.count ?? 0} ${f.count === 1 ? 'event' : 'events'} in the next two months`
            ) : f.enabled ? (
              'Not synced yet'
            ) : (
              'Off'
            )
          }
        >
          <div className="flex items-center gap-2">
            <Toggle checked={f.enabled} onChange={(v) => call('sync', 'updateFeed', f.id, { enabled: v })} />
            <IconButton label="Unsubscribe" size="sm" onClick={() => call('sync', 'removeFeed', f.id)}>
              <Trash2 className="size-3.5" />
            </IconButton>
          </div>
        </Row>
      ))}
      <div className="flex flex-wrap gap-2 px-4 py-3">
        {device?.supported && (
          <Button icon={<CalendarDays className="size-4" />} onClick={() => setDialog('device')}>
            Calendars on this Mac…
          </Button>
        )}
        <Button icon={<Link2 className="size-4" />} onClick={() => setDialog('feed')}>
          {device?.supported ? 'Subscribe by address…' : 'Subscribe to a calendar…'}
        </Button>
        {feeds.length > 0 && (
          <Button variant="ghost" className="ml-auto" disabled={st.syncing} icon={<RefreshCw className={clsx('size-4', st.syncing && 'animate-spin')} />} onClick={() => call('sync', 'syncNow')}>
            {st.syncing ? 'Syncing…' : 'Refresh now'}
          </Button>
        )}
      </div>
      {dialog === 'feed' && <FeedDialog onClose={() => setDialog(null)} />}
      {dialog === 'device' && <DeviceCalendarsDialog onClose={() => setDialog(null)} />}
    </Section>
  )
}

function LinearSection() {
  const st = useProp<SyncState>('sync', 'state', {})
  const l = st.linear ?? {}
  const [key, setKey] = useState('')
  const teamKeys = l.teamKeys ?? []
  const connect = () => {
    if (!key.trim()) return
    call('sync', 'connectLinear', key.trim())
    setKey('')
  }
  return (
    <Section id="linear" title="Linear" description="Issues assigned to you, as tasks. Finishing one here marks it done in Linear.">
      {!l.enabled ? (
        <Row label="Personal API key" description="In Linear: Settings → Account → Security & access → Personal API keys." stacked>
          <div className="flex gap-2">
            <input
              type="password"
              value={key}
              onChange={(e) => setKey(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && connect()}
              placeholder="lin_api_…"
              className="h-8 flex-1 rounded-lg border border-line bg-surface px-2.5 font-mono text-[13px] text-fg shadow-card outline-none placeholder:text-subtle focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none"
            />
            <Button variant="primary" disabled={!key.trim()} icon={<Plug className="size-4" />} onClick={connect}>
              Connect
            </Button>
          </div>
        </Row>
      ) : (
        <>
          <Row
            label={`Connected as ${l.user || '?'}`}
            description={
              l.error ? (
                <span className="text-danger">{l.error}</span>
              ) : l.lastSync ? (
                `${l.summary} · synced at ${new Date(l.lastSync).toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' })}`
              ) : st.syncing ? (
                'Syncing…'
              ) : (
                'Not synced yet'
              )
            }
          >
            <Button size="sm" variant="ghost" disabled={st.syncing} icon={<RefreshCw className={clsx('size-3.5', st.syncing && 'animate-spin')} />} onClick={() => call('sync', 'syncNow')}>
              {st.syncing ? 'Syncing…' : 'Sync now'}
            </Button>
          </Row>
          {(l.teams ?? []).length > 0 && (
            <Row label="Teams" description={teamKeys.length === 0 ? 'All teams are synced until you pick some.' : undefined} stacked>
              <div className="flex flex-wrap gap-x-5 gap-y-2">
                {(l.teams ?? []).map((t) => {
                  const on = teamKeys.length === 0 || teamKeys.includes(t.key)
                  return (
                    <label key={t.key} className="flex items-center gap-2 text-[13.5px] text-fg">
                      <Checkbox
                        checked={on}
                        onChange={(c) => {
                          let keys = teamKeys.length === 0 ? (l.teams ?? []).map((x) => x.key) : [...teamKeys]
                          keys = c ? [...new Set([...keys, t.key])] : keys.filter((k) => k !== t.key)
                          call('sync', 'setLinearTeams', keys)
                        }}
                      />
                      {t.name} <span className="text-subtle">{t.key}</span>
                    </label>
                  )
                })}
              </div>
            </Row>
          )}
          <Row label="Include backlog" description="Off: only Todo and In Progress issues.">
            <Toggle checked={!!l.includeBacklog} onChange={(v) => call('sync', 'setLinearBacklog', v)} />
          </Row>
          <Row label="Disconnect" description="Tasks already brought in stay.">
            <Button size="sm" variant="ghost" icon={<X className="size-3.5" />} onClick={() => call('sync', 'disconnectLinear')}>
              Disconnect
            </Button>
          </Row>
        </>
      )}
    </Section>
  )
}

function Qr({ text }: { text: string }) {
  const cells = useMemo(() => {
    const qr = qrcode(0, 'M')
    qr.addData(text)
    qr.make()
    const n = qr.getModuleCount()
    const dark: [number, number][] = []
    for (let r = 0; r < n; r++) for (let c = 0; c < n; c++) if (qr.isDark(r, c)) dark.push([c, r])
    return { n, dark }
  }, [text])
  return (
    <svg viewBox={`-2 -2 ${cells.n + 4} ${cells.n + 4}`} className="size-40 rounded-lg bg-white" shapeRendering="crispEdges" aria-label="Pairing code">
      {cells.dark.map(([x, y]) => (
        <rect key={`${x},${y}`} x={x} y={y} width={1} height={1} fill="#111" />
      ))}
    </svg>
  )
}

function PhoneSection() {
  const r = useProp<RemoteState>('remote', 'state', {})
  const phones = r.phones ?? []
  return (
    <Section id="phone" title="Phone" description="Today, quick capture and reminders on your phone, over your tailnet.">
      <Row
        label="Phone companion"
        description={r.enabled ? (r.error ? <span className="text-danger">{r.error}</span> : r.running ? `Listening on ${r.listen}${r.streams ? ` · ${r.streams} connected` : ''}` : 'Not running') : undefined}
      >
        <Toggle checked={!!r.enabled} onChange={(v) => call('remote', 'setEnabled', v)} />
      </Row>
      {r.enabled && (
        <>
          <Row label="Address on your tailnet" stacked>
            <div className="flex gap-2">
              <TextSetting value={r.publicUrl ?? ''} onSave={(v) => call('remote', 'setPublicUrl', v)} placeholder="https://this-machine.tailnet.ts.net:7789" mono />
              <Button icon={<Wifi className="size-4" />} onClick={() => call('remote', 'detectTailscale')}>
                Detect
              </Button>
            </div>
          </Row>
          {r.serveCommand && (
            <Row label="Run this once to put it on your tailnet with HTTPS" stacked>
              <CopyLine text={r.serveCommand} onCopy={() => call('remote', 'copyToClipboard', r.serveCommand)} />
            </Row>
          )}
          {r.running && r.pairUrl && (
            <div className="flex items-center gap-5 px-4 py-4">
              <Qr text={r.pairUrl} />
              <div className="min-w-0 flex-1">
                <div className="text-[14px] font-semibold text-fg">Scan with your phone to pair</div>
                <p className="mt-1 text-[13px] leading-snug text-muted">
                  {r.secure
                    ? 'Then add it to the home screen and turn on notifications from its menu.'
                    : 'Set the tailnet https:// address above first: phones only allow notifications and offline use over HTTPS.'}
                </p>
                <Button size="sm" className="mt-3" icon={<RefreshCw className="size-3.5" />} onClick={() => call('remote', 'rotateToken')} title="Phones paired with the old code stop working and have to scan again">
                  New pairing code
                </Button>
              </div>
            </div>
          )}
          {phones.map((p) => (
            <Row key={p.endpoint} label={p.label} description="Gets notifications">
              <IconButton label="Forget this phone" size="sm" onClick={() => call('remote', 'forgetPhone', p.endpoint)}>
                <Trash2 className="size-3.5" />
              </IconButton>
            </Row>
          ))}
          <Row label="Send reminders to the phone">
            <Select
              size="sm"
              value={r.notify ?? 'away'}
              onChange={(v) => set('remote_notify', v)}
              className="min-w-[240px]"
              options={[
                { value: 'away', label: "When I'm away from the computer" },
                { value: 'both', label: 'Always, as well as the desktop' },
                { value: 'desktop', label: 'Never' },
              ]}
            />
          </Row>
          {phones.length > 0 && (
            <div className="px-4 py-3">
              <Button size="sm" icon={<Send className="size-3.5" />} onClick={() => call('remote', 'sendTest')}>
                Send a test notification
              </Button>
            </div>
          )}
        </>
      )}
    </Section>
  )
}

function DataSection() {
  const path = useProp<string>('app', 'dataPath', '')
  const [report, setReport] = useState('')
  return (
    <Section id="data" title="Your data">
      <Row label="Stored on this computer" description={<span className="font-mono text-[12px] break-all select-text">{path}</span>}>
        <Button size="sm" icon={<FolderOpen className="size-3.5" />} onClick={() => call('app', 'openDataFolder')}>
          Open folder
        </Button>
      </Row>
      <Row label="Back up now" description="A copy is also made automatically once a day; the last 14 are kept.">
        <Button size="sm" icon={<Save className="size-3.5" />} onClick={() => call('app', 'backupNow')}>
          Back up
        </Button>
      </Row>
      <Row label="Import from the Task Tumbler web app" description="Only into an empty database. Export with: npm run export-desktop">
        <Button
          size="sm"
          icon={<Upload className="size-3.5" />}
          onClick={async () => {
            const file = await call<string>('shell', 'chooseOpenFile', 'Choose a Task Tumbler export', 'Task Tumbler export (*.json)')
            if (file) setReport(await call<string>('app', 'importExport', file))
          }}
        >
          Import…
        </Button>
      </Row>
      {report && <div className="px-4 py-3 text-[13px] whitespace-pre-line text-fg">{report}</div>}
    </Section>
  )
}
