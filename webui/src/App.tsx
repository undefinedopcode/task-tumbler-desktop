import * as Tooltip from '@radix-ui/react-tooltip'
import { useCallback, useEffect, useRef, useState } from 'react'
import { call, useProp, useSignal } from './bridge/hooks'
import { pageTitles, Sidebar, type Page } from './components/Sidebar'
import { Toasts } from './components/Toasts'
import { isMod, usePlatform } from './lib/platform'
import { Placeholder } from './pages/Placeholder'
import { TodayPage } from './pages/TodayPage'

function initialPage(): Page {
  const hash = location.hash.slice(1) as Page
  return hash in pageTitles ? hash : 'today'
}

export function App() {
  const [page, setPage] = useState<Page>(initialPage)
  const platform = usePlatform()
  const scheme = useProp('shell', 'colorScheme', 'light')
  const captureRef = useRef<HTMLInputElement>(null)

  const showPage = useCallback((p: string) => {
    if (p in pageTitles) {
      setPage(p as Page)
      history.replaceState(null, '', `#${p}`)
    }
  }, [])

  useEffect(() => {
    document.documentElement.dataset.theme = scheme
    document.documentElement.dataset.platform = platform
  }, [scheme, platform])

  // The host's way in: notifications, the tray and screenshot runs.
  useEffect(() => {
    window.tumbler = { showPage }
  }, [showPage])
  useSignal('app', 'pageRequested', showPage)

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const typing = e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement
      if (isMod(e, platform) && e.key.toLowerCase() === 'z' && !typing) {
        e.preventDefault()
        call('app', e.shiftKey ? 'redo' : 'undo')
      } else if (isMod(e, platform) && e.key.toLowerCase() === 'n') {
        e.preventDefault()
        showPage('today')
        setTimeout(() => captureRef.current?.focus(), 0)
      } else if (isMod(e, platform) && /^[1-4]$/.test(e.key)) {
        e.preventDefault()
        showPage((['today', 'tasks', 'schedule', 'notes'] as Page[])[Number(e.key) - 1])
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [platform, showPage])

  return (
    <Tooltip.Provider>
      <div className="flex h-full">
        <Sidebar page={page} onPage={showPage} />
        <main className="min-w-0 flex-1 overflow-y-auto">
          {page === 'today' ? <TodayPage ref={captureRef} onPage={showPage} /> : <Placeholder title={pageTitles[page]} />}
        </main>
      </div>
      <Toasts />
    </Tooltip.Provider>
  )
}
