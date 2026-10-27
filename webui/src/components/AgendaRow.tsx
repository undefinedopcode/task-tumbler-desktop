import clsx from 'clsx'
import { ArrowRight, Check, Clock3, MoreHorizontal, Repeat, SkipForward, Sunrise, Undo2, CalendarX2 } from 'lucide-react'
import { call } from '../bridge/hooks'
import type { AgendaItem } from '../bridge/types'
import { duration, time } from '../lib/format'
import { Menu, MenuContent, MenuItem, MenuLabel, MenuSeparator, MenuTrigger } from './Menu'
import { Chip, IconButton } from './ui'

// One thing on today's timeline: a scheduled block, a routine or an event.
export function AgendaRow({ item }: { item: AgendaItem }) {
  const done = item.state === 'done' || item.state === 'skipped'
  const isEvent = item.kind === 'event'
  const minutes = Math.round((item.end - item.start) / 60_000)
  const complete = () => (item.kind === 'occurrence' ? call('app', 'setOccurrence', item.id, 'completed') : call('app', 'completeSchedule', item.id))

  return (
    <div
      className={clsx(
        'group relative flex items-stretch gap-3 rounded-lg py-2 pr-2 pl-3 transition-colors hover:bg-hover',
        item.state === 'now' && 'bg-accent-soft/60 hover:bg-accent-soft',
      )}
    >
      <div className="w-[68px] shrink-0 pt-px text-right">
        {item.timeless || item.allDay ? (
          <span className="text-[12.5px] text-subtle">{item.allDay ? 'All day' : 'Any time'}</span>
        ) : (
          <>
            <div className={clsx('text-[13px] font-medium tabular-nums', item.state === 'now' ? 'text-accent-text' : 'text-fg')}>{time(item.start)}</div>
            <div className="text-[11.5px] text-subtle tabular-nums">{duration(minutes)}</div>
          </>
        )}
      </div>
      <div className="w-[3px] shrink-0 rounded-full" style={{ background: item.color || (isEvent ? 'var(--tt-subtle)' : 'var(--tt-accent)') }} />
      <div className="min-w-0 flex-1 py-px">
        <div className={clsx('truncate text-[14px]', done ? 'text-subtle line-through' : 'text-fg')}>{item.title}</div>
        <div className="mt-0.5 flex flex-wrap items-center gap-1.5">
          {item.state === 'now' && <Chip tone="accent">Now</Chip>}
          {item.state === 'overdue' && <Chip tone="danger">Missed</Chip>}
          {item.startsSoon && <Chip tone="warning" icon={<Clock3 className="size-3" />}>Soon</Chip>}
          {item.kind === 'occurrence' && <Chip icon={<Repeat className="size-3" />}>Routine</Chip>}
          {isEvent && <span className="text-[12px] text-subtle">Calendar</span>}
          {item.bumpedCount > 1 && <span className="text-[12px] text-subtle">moved {item.bumpedCount}×</span>}
        </div>
      </div>
      {!isEvent && !done && (
        <div className="flex items-center gap-0.5 self-center opacity-0 transition-opacity group-focus-within:opacity-100 group-hover:opacity-100">
          <IconButton label="Done" size="sm" tone="success" onClick={complete}>
            <Check className="size-4" />
          </IconButton>
          <Menu>
            <MenuTrigger asChild>
              <IconButton label="More" size="sm">
                <MoreHorizontal className="size-4" />
              </IconButton>
            </MenuTrigger>
            <MenuContent>
              {item.kind === 'occurrence' ? (
                <MenuItem icon={<SkipForward />} onSelect={() => call('app', 'setOccurrence', item.id, 'skipped')}>
                  Skip today
                </MenuItem>
              ) : (
                <>
                  <MenuLabel>Move</MenuLabel>
                  <MenuItem icon={<Clock3 />} onSelect={() => call('app', 'bump', item.id, 15)}>
                    15 minutes later
                  </MenuItem>
                  <MenuItem icon={<Clock3 />} onSelect={() => call('app', 'bump', item.id, 60)}>
                    An hour later
                  </MenuItem>
                  <MenuItem icon={<ArrowRight />} onSelect={() => call('app', 'bumpToNextSlot', item.id)}>
                    Next free slot
                  </MenuItem>
                  <MenuItem icon={<Sunrise />} onSelect={() => call('app', 'bumpToTomorrow', item.id)}>
                    Tomorrow
                  </MenuItem>
                  <MenuSeparator />
                  <MenuItem icon={<CalendarX2 />} onSelect={() => call('app', 'unschedule', item.id)}>
                    Back to the pile
                  </MenuItem>
                </>
              )}
            </MenuContent>
          </Menu>
        </div>
      )}
      {done && item.kind === 'occurrence' && (
        <div className="self-center opacity-0 group-hover:opacity-100">
          <IconButton label="Not done" size="sm" onClick={() => call('app', 'setOccurrence', item.id, 'pending')}>
            <Undo2 className="size-4" />
          </IconButton>
        </div>
      )}
    </div>
  )
}
