import { ArrowRight, CheckCircle2, Upload, XCircle } from 'lucide-react'
import { useState } from 'react'
import { call } from '../bridge/hooks'
import { Button } from '../components/ui'

// First run: bring tasks over from the web app, or start with a clean slate.
export function OnboardingPage({ onDone }: { onDone: () => void }) {
  const [report, setReport] = useState('')
  const imported = report !== '' && !report.startsWith('Import failed') && !report.startsWith("Couldn't") && !report.startsWith("That file")
  return (
    <div className="flex h-full items-center justify-center bg-bg p-8">
      <div className="w-full max-w-md text-center">
        <svg viewBox="0 0 32 32" className="mx-auto size-20 drop-shadow-lg" aria-hidden>
          <defs>
            <linearGradient id="tt-onb" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0" stopColor="#7c7cf0" />
              <stop offset="1" stopColor="#4a4ac4" />
            </linearGradient>
          </defs>
          <rect width="32" height="32" rx="8" fill="url(#tt-onb)" />
          <path d="M9 11.5h14M9 16h9M9 20.5h11" stroke="white" strokeWidth="2.4" strokeLinecap="round" />
          <circle cx="23" cy="20.5" r="2.6" fill="#ffd166" />
        </svg>
        <h1 className="mt-6 text-[28px] font-semibold tracking-tight text-fg">Welcome to Task Tumbler</h1>
        <p className="mt-2 text-[14.5px] text-muted">Tasks that fit your energy and your day. Everything stays on this computer: no account, no sign-in.</p>
        {!imported && (
          <div className="mt-8 flex justify-center gap-3">
            <Button
              icon={<Upload className="size-4" />}
              onClick={async () => {
                const file = await call<string>('shell', 'chooseOpenFile', 'Choose a Task Tumbler export', 'Task Tumbler export (*.json)')
                if (file) setReport(await call<string>('app', 'importExport', file))
              }}
            >
              Import from the web app…
            </Button>
            <Button
              variant="primary"
              icon={<ArrowRight className="size-4" />}
              onClick={async () => {
                await call('app', 'startFresh')
                onDone()
              }}
            >
              Start fresh
            </Button>
          </div>
        )}
        {!report && <p className="mt-4 text-[12.5px] text-subtle">To import, run “npm run export-desktop -- --out tumbler.json” in the web app's server folder first.</p>}
        {report && (
          <div className={`mt-6 flex items-start gap-2 rounded-xl px-4 py-3 text-left text-[13.5px] whitespace-pre-line ${imported ? 'bg-success-soft' : 'bg-danger-soft'}`}>
            {imported ? <CheckCircle2 className="mt-0.5 size-4 shrink-0 text-success" /> : <XCircle className="mt-0.5 size-4 shrink-0 text-danger" />}
            <span className="text-fg">{report}</span>
          </div>
        )}
        {imported && (
          <Button variant="primary" className="mt-4" icon={<ArrowRight className="size-4" />} onClick={onDone}>
            Go to Today
          </Button>
        )}
      </div>
    </div>
  )
}
