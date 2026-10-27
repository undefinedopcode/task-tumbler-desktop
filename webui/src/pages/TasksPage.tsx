import { CheckCheck, Search, SearchX, Sparkles } from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import { setProp, useProp, useQuery } from '../bridge/hooks'
import type { Category, TaskRowData } from '../bridge/types'
import { Segmented } from '../components/Segmented'
import { Select } from '../components/Select'
import { TaskRow } from '../components/TaskRow'
import { Card, Dot, Empty, Kbd } from '../components/ui'
import { isMod, keys, usePlatform } from '../lib/platform'

type Filter = 'active' | 'completed' | 'all'

export function TasksPage({ selectedTask }: { selectedTask: string | null }) {
  const filter = useProp<Filter>('app.tasks', 'filter', 'active')
  const categoryId = useProp<string>('app.tasks', 'categoryId', '')
  const categories = useProp<Category[]>('app', 'categories', [])
  const rows = useQuery<TaskRowData[]>('app.tasks', 'rows', [], [], ['rowsChanged'])
  const platform = usePlatform()
  const [search, setSearch] = useState('')
  const searchRef = useRef<HTMLInputElement>(null)

  // Typing filters as you go; the model does the matching.
  useEffect(() => {
    const t = setTimeout(() => setProp('app.tasks', 'search', search), 120)
    return () => clearTimeout(t)
  }, [search])
  useEffect(() => () => void setProp('app.tasks', 'search', ''), [])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (isMod(e, platform) && e.key.toLowerCase() === 'f') {
        e.preventDefault()
        searchRef.current?.focus()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [platform])

  // One-off tasks first, routines after, as the model sorts them.
  const groups = useMemo(() => {
    if (filter === 'completed' || search) return [{ title: '', rows }]
    const oneOff = rows.filter((r) => !r.isRecurring)
    const routines = rows.filter((r) => r.isRecurring)
    return [
      { title: '', rows: oneOff },
      { title: 'Routines', rows: routines },
    ].filter((g) => g.rows.length)
  }, [rows, filter, search])

  return (
    <div className="mx-auto flex w-full max-w-[920px] flex-col gap-4 px-8 pt-8 pb-16">
      <header className="flex items-end gap-4">
        <div className="flex-1">
          <h1 className="text-[26px] leading-tight font-semibold tracking-tight text-fg">Tasks</h1>
          <div className="mt-1 text-[13.5px] text-muted">
            {rows.length} {filter === 'completed' ? 'finished' : filter === 'all' ? 'in total' : 'to do'}
            {search && ` matching “${search}”`}
          </div>
        </div>
      </header>

      <div className="flex flex-wrap items-center gap-2">
        <div className="relative min-w-[220px] flex-1">
          <Search className="pointer-events-none absolute top-2 left-2.5 size-4 text-subtle" />
          <input
            ref={searchRef}
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            onKeyDown={(e) => e.key === 'Escape' && setSearch('')}
            placeholder="Search titles, details, #tags…"
            className="h-8 w-full rounded-lg border border-line bg-surface pr-14 pl-8 text-[13.5px] text-fg shadow-card outline-none placeholder:text-subtle focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none"
          />
          <span className="pointer-events-none absolute top-1.5 right-2">
            <Kbd>{keys(platform, 'Mod+F')}</Kbd>
          </span>
        </div>
        <Segmented
          value={filter}
          onChange={(f) => setProp('app.tasks', 'filter', f)}
          options={[
            { value: 'active', label: 'To do' },
            { value: 'completed', label: 'Done' },
            { value: 'all', label: 'All' },
          ]}
        />
        <Select
          aria-label="Category"
          value={categoryId}
          onChange={(id) => setProp('app.tasks', 'categoryId', id)}
          className="min-w-[160px]"
          options={[{ value: '', label: 'All categories' }, ...categories.map((c) => ({ value: c.id, label: c.name, icon: <Dot color={c.color} /> }))]}
        />
      </div>

      {rows.length === 0 ? (
        search ? (
          <Empty icon={<SearchX className="size-6" />} title={`Nothing matches “${search}”`} hint="Try fewer words, or a #tag." />
        ) : filter === 'completed' ? (
          <Empty icon={<Sparkles className="size-6" />} title="Nothing finished yet" hint="Ticked-off tasks collect here." />
        ) : (
          <Empty icon={<CheckCheck className="size-6" />} title="Nothing to do" hint={`Enjoy it, or add the next thing with ${keys(platform, 'Mod+N')}.`} />
        )
      ) : (
        groups.map((g) => (
          <section key={g.title || 'main'}>
            {g.title && <h2 className="mt-2 mb-2 px-1 text-[12.5px] font-semibold tracking-wide text-muted uppercase">{g.title}</h2>}
            <Card className="p-1.5">
              {g.rows.map((r) => (
                <TaskRow key={r.taskId} task={r} selected={r.taskId === selectedTask} />
              ))}
            </Card>
          </section>
        ))
      )}
    </div>
  )
}
