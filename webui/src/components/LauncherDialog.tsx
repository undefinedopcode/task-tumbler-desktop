import clsx from 'clsx'
import { Search } from 'lucide-react'
import { useEffect, useMemo, useState } from 'react'
import { call } from '../bridge/hooks'
import { Dialog, TextField } from './Dialog'
import { Segmented } from './Segmented'
import { ActionIcon } from './TaskDetailPanel'
import { Button, Toggle } from './ui'

type Kind = 'url' | 'file' | 'app' | 'command'
interface App {
  id: string
  name: string
  icon: string
}

// Something to open when a task starts: a link, a file, an app or a command.
export function LauncherDialog({ taskId, open, onOpenChange }: { taskId: string; open: boolean; onOpenChange: (o: boolean) => void }) {
  const [kind, setKind] = useState<Kind>('url')
  const [url, setUrl] = useState('')
  const [file, setFile] = useState('')
  const [app, setApp] = useState('')
  const [command, setCommand] = useState('')
  const [onStart, setOnStart] = useState(true)
  const [apps, setApps] = useState<App[]>([])
  const [filter, setFilter] = useState('')

  useEffect(() => {
    if (!open) return
    setKind('url')
    setUrl('')
    setFile('')
    setApp('')
    setCommand('')
    setFilter('')
    setOnStart(true)
  }, [open])
  useEffect(() => {
    if (open && kind === 'app' && apps.length === 0) call<App[]>('app', 'installedApps').then(setApps)
  }, [open, kind, apps.length])

  const shown = useMemo(() => apps.filter((a) => a.name.toLowerCase().includes(filter.toLowerCase())).slice(0, 80), [apps, filter])
  const target = { url, file, app, command }[kind].trim()

  const add = async () => {
    if (!target) return
    const id = await call<string>('app', 'addAction', taskId, kind, target, onStart)
    if (id) onOpenChange(false)
  }

  return (
    <Dialog
      open={open}
      onOpenChange={onOpenChange}
      title="Open when I start"
      description="Have what you need ready the moment you begin."
      footer={
        <>
          <Button variant="ghost" onClick={() => onOpenChange(false)}>
            Cancel
          </Button>
          <Button variant="primary" disabled={!target} onClick={add}>
            Add
          </Button>
        </>
      }
    >
      <Segmented
        value={kind}
        onChange={setKind}
        className="mb-4"
        options={[
          { value: 'url', label: 'Link' },
          { value: 'file', label: 'File' },
          { value: 'app', label: 'App' },
          { value: 'command', label: 'Command' },
        ]}
      />
      {kind === 'url' && <TextField autoFocus value={url} onChange={(e) => setUrl(e.target.value)} placeholder="https://docs.example.com/the-report" />}
      {kind === 'file' && (
        <div className="flex gap-2">
          <TextField value={file} onChange={(e) => setFile(e.target.value)} placeholder="A document, a folder, a playlist…" />
          <Button
            onClick={async () => {
              const chosen = await call<string>('shell', 'chooseOpenFile', 'Choose a file', '')
              if (chosen) setFile(decodeURIComponent(new URL(chosen).pathname))
            }}
          >
            Choose…
          </Button>
        </div>
      )}
      {kind === 'app' && (
        <div>
          <div className="relative">
            <Search className="pointer-events-none absolute top-2 left-2.5 size-4 text-subtle" />
            <TextField autoFocus value={filter} onChange={(e) => setFilter(e.target.value)} placeholder="Find an app" className="pl-8" />
          </div>
          <div className="mt-2 h-56 overflow-y-auto rounded-lg border border-line p-1">
            {shown.map((a) => (
              <button
                key={a.id}
                onClick={() => setApp(a.id)}
                className={clsx(
                  'flex h-9 w-full items-center gap-2.5 rounded-md px-2 text-left text-[13.5px]',
                  app === a.id ? 'bg-accent-soft text-accent-text' : 'text-fg hover:bg-hover',
                )}
              >
                <ActionIcon action={{ kind: 'app', icon: a.icon }} />
                {a.name}
              </button>
            ))}
            {apps.length === 0 && <div className="p-3 text-[13px] text-subtle">Looking for apps…</div>}
          </div>
        </div>
      )}
      {kind === 'command' && (
        <>
          <TextField autoFocus value={command} onChange={(e) => setCommand(e.target.value)} placeholder="kate ~/notes/report.md" className="font-mono" />
          <p className="mt-2 text-[12.5px] text-muted">
            Runs exactly this, without a shell (no pipes or variables; quote paths with spaces). You'll be asked once before it first runs, and again if it changes.
          </p>
        </>
      )}
      <label className="mt-4 flex items-center gap-3 text-[13.5px] text-fg">
        <Toggle checked={onStart} onChange={setOnStart} />
        Open automatically when I start this task
      </label>
    </Dialog>
  )
}
