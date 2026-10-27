import clsx from 'clsx'
import { ArrowUpRight, Download, Eye, MoreHorizontal, NotebookPen, PenLine, Plus, Search, SearchX, Trash2 } from 'lucide-react'
import { useCallback, useEffect, useRef, useState } from 'react'
import { call, useQuery, useSignal } from '../bridge/hooks'
import type { NoteDetail, NoteSummary } from '../bridge/types'
import { Markdown, Snippet } from '../components/Markdown'
import { Menu, MenuContent, MenuItem, MenuSeparator, MenuTrigger } from '../components/Menu'
import { Segmented } from '../components/Segmented'
import { Select } from '../components/Select'
import { TagsInput } from '../components/TaskDetailPanel'
import { Button, Empty, IconButton, Kbd } from '../components/ui'
import { time } from '../lib/format'
import { useNav } from '../lib/nav'
import { isMod, keys, usePlatform } from '../lib/platform'

const swatches = ['', '#6366f1', '#0ea5e9', '#10b981', '#f59e0b', '#ef4444', '#ec4899', '#a855f7']

function when(ms: number): string {
  const d = new Date(ms)
  const today = new Date()
  if (d.toDateString() === today.toDateString()) return time(ms)
  return d.toLocaleDateString(undefined, { day: 'numeric', month: 'short', year: d.getFullYear() === today.getFullYear() ? undefined : 'numeric' })
}

// Every note, newest first, beside the one being edited.
export function NotesPage({ noteId, onSelect }: { noteId: string | null; onSelect: (id: string | null) => void }) {
  const platform = usePlatform()
  const [search, setSearch] = useState('')
  const [query, setQuery] = useState('')
  const [tag, setTag] = useState('')
  const notes = useQuery<NoteSummary[]>('app', 'notes', [query, tag, ''], [])
  const tags = useQuery<string[]>('app', 'noteTags', [], [])
  const searchRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    const t = setTimeout(() => setQuery(search), 150)
    return () => clearTimeout(t)
  }, [search])
  useEffect(() => {
    if (tag && tags.length && !tags.includes(tag)) setTag('')
  }, [tags, tag])

  const newNote = useCallback(async () => {
    const id = await call<string>('app', 'createNote', { tags: tag ? [tag] : [] })
    if (id) onSelect(id)
  }, [tag, onSelect])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (isMod(e, platform) && e.shiftKey && e.key.toLowerCase() === 'n') {
        e.preventDefault()
        newNote()
      } else if (isMod(e, platform) && !e.shiftKey && e.key.toLowerCase() === 'f') {
        e.preventDefault()
        searchRef.current?.focus()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [platform, newNote])

  return (
    <div className="flex h-full">
      <div className="flex w-[320px] shrink-0 flex-col border-r border-line">
        <div className="flex items-center gap-2 px-5 pt-8 pb-3">
          <h1 className="flex-1 text-[22px] font-semibold tracking-tight">Notes</h1>
          <IconButton label={`New note (${keys(platform, 'Mod+Shift+N')})`} onClick={newNote}>
            <Plus className="size-4" />
          </IconButton>
        </div>
        <div className="px-4 pb-2">
          <div className="relative">
            <Search className="pointer-events-none absolute top-2 left-2.5 size-4 text-subtle" />
            <input
              ref={searchRef}
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              onKeyDown={(e) => e.key === 'Escape' && setSearch('')}
              placeholder="Search notes…"
              className="h-8 w-full rounded-lg border border-line bg-surface pr-2 pl-8 text-[13.5px] text-fg shadow-card outline-none placeholder:text-subtle focus-visible:border-accent focus-visible:ring-4 focus-visible:ring-accent-soft focus-visible:outline-none"
            />
          </div>
          {tags.length > 0 && (
            <div className="mt-2 flex flex-wrap gap-1">
              {tags.map((t) => (
                <button
                  key={t}
                  onClick={() => setTag(tag === t ? '' : t)}
                  className={clsx(
                    'h-6 rounded-md px-2 text-[12px] font-medium transition-colors',
                    tag === t ? 'bg-accent text-accent-fg' : 'bg-hover text-muted hover:text-fg',
                  )}
                >
                  #{t}
                </button>
              ))}
            </div>
          )}
        </div>
        <div className="min-h-0 flex-1 overflow-y-auto px-2 pb-4">
          {notes.length === 0 && (
            <div className="px-3 py-10 text-center text-[13px] text-muted">
              {query ? (
                <>
                  <SearchX className="mx-auto mb-2 size-5 text-subtle" />
                  Nothing matches “{query}”
                </>
              ) : tag ? (
                `No notes tagged #${tag}`
              ) : (
                'No notes yet'
              )}
            </div>
          )}
          {notes.map((n) => (
            <button
              key={n.id}
              onClick={() => onSelect(n.id)}
              className={clsx(
                'relative mb-0.5 flex w-full flex-col gap-0.5 rounded-lg py-2 pr-3 pl-4 text-left transition-colors',
                n.id === noteId ? 'bg-surface shadow-card' : 'hover:bg-hover',
              )}
            >
              {n.color && <span className="absolute inset-y-2.5 left-1.5 w-[3px] rounded-full" style={{ background: n.color }} />}
              <span className="flex items-baseline gap-2">
                <span className="flex-1 truncate text-[13.5px] font-medium text-fg">{n.title || 'Untitled'}</span>
                <span className="shrink-0 text-[11.5px] text-subtle">{when(n.updatedAt)}</span>
              </span>
              {n.snippet && (
                <span className="line-clamp-2 text-[12.5px] leading-snug text-muted">
                  <Snippet html={n.snippet} />
                </span>
              )}
              {(n.tags.length > 0 || n.taskTitle) && (
                <span className="mt-0.5 flex flex-wrap gap-x-2 text-[11.5px] text-subtle">
                  {n.tags.map((t) => (
                    <span key={t}>#{t}</span>
                  ))}
                  {n.taskTitle && <span className="truncate">↳ {n.taskTitle}</span>}
                </span>
              )}
            </button>
          ))}
        </div>
      </div>
      <div className="min-w-0 flex-1">
        {noteId ? (
          <NoteEditor key={noteId} noteId={noteId} onGone={() => onSelect(null)} />
        ) : (
          <div className="flex h-full items-center justify-center p-8">
            <div className="max-w-sm">
              <Empty
                icon={<NotebookPen className="size-6" />}
                title={notes.length ? 'Pick a note, or start a new one' : 'Somewhere for meeting notes, ideas and things to remember'}
                hint={`Markdown works. New note: ${keys(platform, 'Mod+Shift+N')}`}
              />
              <div className="mt-3 flex justify-center">
                <Button variant="primary" icon={<Plus className="size-4" />} onClick={newNote}>
                  New note
                </Button>
              </div>
            </div>
          </div>
        )}
      </div>
    </div>
  )
}

// One note. Saves itself a moment after you stop typing, and when you leave.
function NoteEditor({ noteId, onGone }: { noteId: string; onGone: () => void }) {
  const nav = useNav()
  const platform = usePlatform()
  const [note, setNote] = useState<NoteDetail | null>(null)
  const [title, setTitle] = useState('')
  const [content, setContent] = useState('')
  const [preview, setPreview] = useState(false)
  const dirty = useRef(false)
  const latest = useRef({ title, content })
  latest.current = { title, content }
  const timer = useRef<ReturnType<typeof setTimeout>>(undefined)
  const titleRef = useRef<HTMLInputElement>(null)
  const bodyRef = useRef<HTMLTextAreaElement>(null)
  const taskChoices = useQuery<{ id: string; title: string }[]>('app', 'noteTaskChoices', [], [])

  const save = useCallback(() => {
    clearTimeout(timer.current)
    if (!dirty.current) return
    dirty.current = false
    call('app', 'saveNote', noteId, { title: latest.current.title, content: latest.current.content })
  }, [noteId])
  const edited = () => {
    dirty.current = true
    clearTimeout(timer.current)
    timer.current = setTimeout(save, 800)
  }

  const load = useCallback(
    async (first: boolean) => {
      const fresh = await call<NoteDetail>('app', 'noteDetail', noteId)
      if (!fresh.id) return onGone()
      setNote(fresh)
      // Changed elsewhere (an undo, say): take it, unless there are edits here.
      if (first || !dirty.current) {
        setTitle(fresh.title)
        setContent(fresh.content)
      }
      if (first) {
        requestAnimationFrame(() => {
          if (fresh.title === 'Untitled note' && !fresh.content) titleRef.current?.select()
          else bodyRef.current?.focus()
        })
      }
    },
    [noteId, onGone],
  )
  useEffect(() => {
    load(true)
    return () => save()
  }, [load, save])
  useSignal(
    'app',
    'changed',
    useCallback((tables: string[]) => tables.includes('notes') && load(false), [load]),
  )
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (isMod(e, platform) && e.shiftKey && e.key.toLowerCase() === 'p') {
        e.preventDefault()
        setPreview((p) => !p)
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [platform])

  if (!note) return null
  const saveField = (fields: Record<string, unknown>) => {
    save()
    call('app', 'saveNote', noteId, fields)
  }
  const choices = [...taskChoices]
  if (note.taskId && !choices.some((t) => t.id === note.taskId)) choices.unshift({ id: note.taskId, title: note.taskTitle })

  return (
    <div className="flex h-full flex-col">
      <div className="flex items-center gap-2 px-8 pt-6">
        <Segmented
          size="sm"
          value={preview ? 'preview' : 'edit'}
          onChange={(v) => setPreview(v === 'preview')}
          options={[
            { value: 'edit', label: <span className="flex items-center gap-1"><PenLine className="size-3.5" /> Write</span>, title: keys(platform, 'Mod+Shift+P') },
            { value: 'preview', label: <span className="flex items-center gap-1"><Eye className="size-3.5" /> Preview</span>, title: keys(platform, 'Mod+Shift+P') },
          ]}
        />
        <span className="ml-auto text-[12px] text-subtle">Edited {when(note.updatedAt)}</span>
        <Menu>
          <MenuTrigger asChild>
            <IconButton label="More" size="sm">
              <MoreHorizontal className="size-4" />
            </IconButton>
          </MenuTrigger>
          <MenuContent>
            <MenuItem
              icon={<Download />}
              onSelect={async () => {
                save()
                const name = `${(title || 'note').replace(/[/\\:*?"<>|]+/g, '-')}.md`
                const url = await call<string>('shell', 'chooseSaveFile', 'Export note', name, 'Markdown (*.md)')
                if (url) call('app', 'exportNote', noteId, url)
              }}
            >
              Export as Markdown…
            </MenuItem>
            <MenuSeparator />
            <MenuItem
              icon={<Trash2 />}
              danger
              onSelect={() => {
                dirty.current = false
                clearTimeout(timer.current)
                call('app', 'deleteNote', noteId)
              }}
            >
              Delete note
            </MenuItem>
          </MenuContent>
        </Menu>
      </div>
      <div className="px-8 pt-3">
        <input
          ref={titleRef}
          value={title}
          onChange={(e) => {
            setTitle(e.target.value)
            edited()
          }}
          onBlur={save}
          onKeyDown={(e) => e.key === 'Enter' && bodyRef.current?.focus()}
          placeholder="Title"
          className="w-full bg-transparent text-[24px] font-semibold tracking-tight text-fg outline-none placeholder:text-subtle focus-visible:outline-none"
        />
        <div className="mt-2 flex flex-wrap items-center gap-x-4 gap-y-2 border-b border-line pb-3">
          <div className="min-w-[180px] flex-1">
            <TagsInput tags={note.tags} onSave={(tags) => saveField({ tags })} />
          </div>
          <div className="flex items-center gap-1">
            <Select
              size="sm"
              value={note.taskId}
              onChange={(taskId) => saveField({ taskId })}
              className="max-w-[240px]"
              options={[{ value: '', label: 'Not about a task' }, ...choices.map((t) => ({ value: t.id, label: t.title }))]}
            />
            {note.taskId && (
              <IconButton label="Open the task" size="sm" onClick={() => nav.openTask(note.taskId)}>
                <ArrowUpRight className="size-4" />
              </IconButton>
            )}
          </div>
          <div className="flex gap-1">
            {swatches.map((c) => (
              <button
                key={c || 'none'}
                aria-label={c ? `Colour ${c}` : 'No colour'}
                onClick={() => saveField({ color: c })}
                className={clsx(
                  'relative size-[18px] rounded-full border transition-transform hover:scale-110',
                  c ? 'border-transparent' : 'border-line-strong',
                  (note.color || '') === c && 'ring-2 ring-fg ring-offset-2 ring-offset-bg',
                )}
                style={{ background: c || 'transparent' }}
              >
                {!c && <span className="absolute top-1/2 left-1/2 h-px w-3 -translate-x-1/2 -translate-y-1/2 -rotate-45 bg-subtle" />}
              </button>
            ))}
          </div>
        </div>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto px-8 py-4">
        {preview ? (
          content.trim() ? <Markdown text={content} /> : <p className="text-[14px] text-subtle italic">Nothing written yet.</p>
        ) : (
          <textarea
            ref={bodyRef}
            value={content}
            onChange={(e) => {
              setContent(e.target.value)
              edited()
            }}
            onBlur={save}
            placeholder="Write here. Markdown works: # headings, **bold**, - lists, [links](https://…)"
            className="h-full min-h-[300px] w-full resize-none bg-transparent text-[14.5px] leading-relaxed text-fg outline-none placeholder:text-subtle focus-visible:outline-none"
            spellCheck
          />
        )}
      </div>
      <div className="flex items-center gap-2 border-t border-line px-8 py-1.5 text-[11.5px] text-subtle">
        <Kbd>{keys(platform, 'Mod+Shift+P')}</Kbd> preview
        <span className="ml-auto">{content.trim() ? `${content.trim().split(/\s+/).length} words` : ''}</span>
      </div>
    </div>
  )
}
