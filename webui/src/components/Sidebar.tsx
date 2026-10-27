import clsx from 'clsx'
import {
  BarChart3,
  BatteryMedium,
  CalendarCheck2,
  CalendarDays,
  ListTodo,
  Moon,
  NotebookPen,
  Settings,
  Sun,
  Tags,
  type LucideIcon,
} from 'lucide-react'
import { useProp } from '../bridge/hooks'
import type { Today } from '../bridge/types'

export type Page =
  | 'today'
  | 'tasks'
  | 'schedule'
  | 'notes'
  | 'energy'
  | 'stats'
  | 'categories'
  | 'closeout'
  | 'review'
  | 'settings'

const main: { page: Page; label: string; icon: LucideIcon }[] = [
  { page: 'today', label: 'Today', icon: Sun },
  { page: 'tasks', label: 'Tasks', icon: ListTodo },
  { page: 'schedule', label: 'Schedule', icon: CalendarDays },
  { page: 'notes', label: 'Notes', icon: NotebookPen },
]
const insight: { page: Page; label: string; icon: LucideIcon }[] = [
  { page: 'energy', label: 'Energy', icon: BatteryMedium },
  { page: 'stats', label: 'Stats', icon: BarChart3 },
  { page: 'categories', label: 'Categories', icon: Tags },
]

export const pageTitles: Record<Page, string> = {
  today: 'Today',
  tasks: 'Tasks',
  schedule: 'Schedule',
  notes: 'Notes',
  energy: 'Energy',
  stats: 'Stats',
  categories: 'Categories',
  closeout: 'Close out the day',
  review: 'Week review',
  settings: 'Settings',
}

export function Sidebar({ page, onPage }: { page: Page; onPage: (p: Page) => void }) {
  const today = useProp<Today | null>('app', 'today', null)
  const readyCount = today?.readyTotal ?? 0
  const item = (p: Page, label: string, Icon: LucideIcon, badge?: number | string, dot?: boolean) => (
    <button
      key={p}
      onClick={() => onPage(p)}
      aria-current={page === p ? 'page' : undefined}
      className={clsx(
        'group flex h-8 w-full items-center gap-2.5 rounded-lg px-2.5 text-[13.5px] font-medium transition-colors',
        page === p ? 'bg-surface text-fg shadow-card' : 'text-muted hover:bg-hover hover:text-fg',
      )}
    >
      <Icon className={clsx('size-[17px]', page === p ? 'text-accent' : 'text-subtle group-hover:text-muted')} strokeWidth={2} />
      <span className="flex-1 text-left">{label}</span>
      {dot && <span className="size-1.5 rounded-full bg-accent" />}
      {badge !== undefined && badge !== 0 && <span className="text-[12px] text-subtle tabular-nums">{badge}</span>}
    </button>
  )

  return (
    <nav className="flex h-full w-[220px] shrink-0 flex-col border-r border-line bg-sidebar px-3 pt-4 pb-3">
      <div className="mb-5 flex items-center gap-2.5 px-2">
        <Logo />
        <span className="text-[15px] font-semibold tracking-tight">Task Tumbler</span>
      </div>
      <div className="flex flex-col gap-0.5">
        {main.map((m) => item(m.page, m.label, m.icon, m.page === 'tasks' ? readyCount : undefined))}
      </div>
      <div className="mt-5 mb-1.5 px-2.5 text-[11.5px] font-semibold tracking-wide text-subtle uppercase">Insight</div>
      <div className="flex flex-col gap-0.5">{insight.map((m) => item(m.page, m.label, m.icon))}</div>
      <div className="mt-5 mb-1.5 px-2.5 text-[11.5px] font-semibold tracking-wide text-subtle uppercase">Reflect</div>
      <div className="flex flex-col gap-0.5">
        {item('closeout', 'Close out', Moon, undefined, today?.closeOutDue)}
        {item('review', 'Week review', CalendarCheck2, undefined, today?.reviewDue)}
      </div>
      <div className="mt-auto flex flex-col gap-0.5">{item('settings', 'Settings', Settings)}</div>
    </nav>
  )
}

function Logo() {
  return (
    <svg viewBox="0 0 32 32" className="size-7" aria-hidden>
      <defs>
        <linearGradient id="tt-logo" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#7c7cf0" />
          <stop offset="1" stopColor="#4a4ac4" />
        </linearGradient>
      </defs>
      <rect width="32" height="32" rx="8" fill="url(#tt-logo)" />
      <path d="M9 11.5h14M9 16h9M9 20.5h11" stroke="white" strokeWidth="2.4" strokeLinecap="round" />
      <circle cx="23" cy="20.5" r="2.6" fill="#ffd166" />
    </svg>
  )
}
