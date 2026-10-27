const timeFmt = new Intl.DateTimeFormat(undefined, { hour: 'numeric', minute: '2-digit' })

export function time(ms: number): string {
  return timeFmt.format(ms)
}

export function timeRange(start: number, end: number): string {
  return `${time(start)} – ${time(end)}`
}

// 95 → "1h 35m"
export function duration(minutes: number): string {
  if (!minutes || minutes < 1) return '0m'
  const h = Math.floor(minutes / 60)
  const m = Math.round(minutes % 60)
  if (!h) return `${m}m`
  return m ? `${h}h ${m}m` : `${h}h`
}

// Elapsed since `startedAt` as h:mm:ss or m:ss.
export function elapsed(startedAt: number, now = Date.now()): string {
  const s = Math.max(0, Math.floor((now - startedAt) / 1000))
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const sec = String(s % 60).padStart(2, '0')
  return h ? `${h}:${String(m).padStart(2, '0')}:${sec}` : `${m}:${sec}`
}

export function dueLabel(iso: string): { text: string; tone: 'danger' | 'warning' | 'muted' } | null {
  if (!iso) return null
  const today = new Date()
  today.setHours(0, 0, 0, 0)
  const due = new Date(`${iso}T00:00:00`)
  const days = Math.round((due.getTime() - today.getTime()) / 86_400_000)
  if (days < 0) return { text: days === -1 ? 'Due yesterday' : `${-days} days overdue`, tone: 'danger' }
  if (days === 0) return { text: 'Due today', tone: 'warning' }
  if (days === 1) return { text: 'Due tomorrow', tone: 'warning' }
  if (days < 7) return { text: `Due ${due.toLocaleDateString(undefined, { weekday: 'short' })}`, tone: 'muted' }
  return { text: `Due ${due.toLocaleDateString(undefined, { day: 'numeric', month: 'short' })}`, tone: 'muted' }
}

export function greeting(now = new Date()): string {
  const h = now.getHours()
  if (h < 5) return 'Still up'
  if (h < 12) return 'Good morning'
  if (h < 18) return 'Good afternoon'
  return 'Good evening'
}
