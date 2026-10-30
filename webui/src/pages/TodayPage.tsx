import { ChevronDown, Coffee, Inbox, Moon, PartyPopper, CalendarCheck2, Sparkles, Zap } from 'lucide-react'
import { forwardRef, useState } from 'react'
import { call, useProp } from '../bridge/hooks'
import type { Away, EnergyState, Today, Working } from '../bridge/types'
import { AgendaRow } from '../components/AgendaRow'
import { Banner } from '../components/Banner'
import { CalendarChanges } from '../components/CalendarChanges'
import { TimeChecks } from '../components/TimeChecks'
import { CaptureBar } from '../components/CaptureBar'
import { EnergyCheckIn } from '../components/EnergyCheckIn'
import { ReadyRow } from '../components/ReadyRow'
import { WorkingCard } from '../components/WorkingCard'
import { Button, Card, Chip, Dot, Empty, SectionHeader } from '../components/ui'
import { greeting } from '../lib/format'
import { useNav } from '../lib/nav'
import type { Page } from '../components/Sidebar'

export const TodayPage = forwardRef<HTMLInputElement, { onPage: (p: Page) => void }>(function TodayPage({ onPage }, captureRef) {
  const today = useProp<Today | null>('app', 'today', null)
  const energy = useProp<EnergyState>('app', 'energy', {})
  const working = useProp<Working>('app', 'working', {})
  const away = useProp<Away>('app', 'away', {})
  const [showDone, setShowDone] = useState(false)
  const nav = useNav()
  if (!today) return null

  const timeline = [...today.overdue, ...today.now, ...today.upcoming]
  const left = today.now.length + today.upcoming.length + today.overdue.length
  const summary = [
    left ? `${left} ${left === 1 ? 'thing' : 'things'} on the plan` : 'Nothing planned',
    today.readyTotal ? `${today.readyTotal} ready to start` : null,
    today.done.length ? `${today.done.length} done` : null,
  ]
    .filter(Boolean)
    .join(' · ')

  return (
    <div className="mx-auto flex w-full max-w-[1080px] flex-col gap-5 px-8 pt-8 pb-16">
      <header className="flex items-end gap-4">
        <div className="min-w-0 flex-1">
          <div className="text-[13px] font-medium text-muted">{greeting()}</div>
          <h1 className="mt-0.5 text-[26px] leading-tight font-semibold tracking-tight text-fg">{today.dateLabel}</h1>
          <div className="mt-1 text-[13.5px] text-muted">{summary}</div>
        </div>
        <div className="flex flex-wrap items-center justify-end gap-1.5 pb-1">
          {today.allDay.map((e) => (
            <Chip key={e.id} className="h-6 px-2 text-[12px]" icon={<Dot color={e.color} className="size-1.5" />}>
              {e.title}
            </Chip>
          ))}
          {energy.level !== undefined && (
            <Chip tone={energy.level <= 4 ? 'warning' : 'success'} className="h-6 px-2 text-[12px]" icon={<Zap className="size-3" />}>
              Energy: {energy.label}
            </Chip>
          )}
        </div>
      </header>

      <CaptureBar ref={captureRef} />

      <CalendarChanges changes={today.calendarChanges ?? []} />
      <TimeChecks checks={today.timeChecks ?? []} />

      {away.minutes !== undefined && (
        <Banner
          icon={<Coffee />}
          actions={
            <>
              <Button size="sm" variant="ghost" onClick={() => call('app', 'resolveAway', 'trim')}>
                Stop then
              </Button>
              <Button size="sm" variant="secondary" onClick={() => call('app', 'resolveAway', 'trimRestart')}>
                Leave the break out
              </Button>
              <Button size="sm" variant="primary" onClick={() => call('app', 'resolveAway', 'keep')}>
                Keep the time
              </Button>
            </>
          }
        >
          Welcome back — you were away for {away.minutes} min. Still working on <b>{away.title}</b>?
        </Banner>
      )}
      {today.closeOutDue && (
        <Banner
          icon={<Moon />}
          tone="success"
          actions={
            <>
              <Button size="sm" variant="ghost" onClick={() => call('app', 'finishCloseOut', true)}>
                Not today
              </Button>
              <Button size="sm" variant="primary" onClick={() => onPage('closeout')}>
                Close out
              </Button>
            </>
          }
        >
          Winding down? Take a couple of minutes to close out the day.
        </Banner>
      )}
      {today.reviewDue && !today.closeOutDue && (
        <Banner
          icon={<CalendarCheck2 />}
          actions={
            <>
              <Button size="sm" variant="ghost" onClick={() => call('app', 'finishReview', true)}>
                Skip this week
              </Button>
              <Button size="sm" variant="primary" onClick={() => onPage('review')}>
                Review the week
              </Button>
            </>
          }
        >
          Time for the weekly review: a few minutes to look back and tidy up.
        </Banner>
      )}

      <WorkingCard />
      {energy.due && !working.id && <EnergyCheckIn />}

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-[minmax(0,1.05fr)_minmax(0,1fr)]">
        <section>
          <SectionHeader
            title="Plan"
            count={left || undefined}
            action={
              <Button size="sm" variant="ghost" icon={<Sparkles className="size-3.5" />} onClick={() => nav.openPlan()}>
                Plan my day
              </Button>
            }
          />
          {timeline.length === 0 && today.done.length === 0 ? (
            <Empty icon={<Coffee className="size-6" />} title="A blank slate" hint="Start something from the right, or plan the day." />
          ) : (
            <Card className="p-1.5">
              {timeline.length === 0 && (
                <div className="flex items-center gap-2 px-3 py-3 text-[13.5px] text-muted">
                  <PartyPopper className="size-4 text-success" /> Everything planned for today is done.
                </div>
              )}
              {timeline.map((item) => (
                <AgendaRow key={`${item.kind}-${item.id}`} item={item} />
              ))}
              {today.done.length > 0 && (
                <>
                  <button
                    onClick={() => setShowDone(!showDone)}
                    className="mt-1 flex w-full items-center gap-1.5 rounded-lg px-3 py-2 text-[12.5px] font-medium text-muted hover:bg-hover"
                  >
                    <ChevronDown className={`size-3.5 transition-transform ${showDone ? '' : '-rotate-90'}`} />
                    {today.done.length} done today
                  </button>
                  {showDone && today.done.map((item) => <AgendaRow key={`${item.kind}-${item.id}`} item={item} />)}
                </>
              )}
            </Card>
          )}
        </section>

        <section>
          <SectionHeader
            title="Ready to start"
            count={today.readyTotal || undefined}
            action={
              today.readyTotal > today.ready.length && (
                <Button size="sm" variant="ghost" onClick={() => onPage('tasks')}>
                  See all
                </Button>
              )
            }
          />
          {today.ready.length === 0 ? (
            <Empty icon={<Inbox className="size-6" />} title="Nothing waiting" hint="Add a task above and it'll show up here." />
          ) : (
            <Card className="p-1.5">
              {today.ready.map((t) => (
                <ReadyRow key={t.id} task={t} />
              ))}
            </Card>
          )}
        </section>
      </div>
    </div>
  )
})
