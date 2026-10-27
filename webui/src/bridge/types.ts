// What the C++ side sends, as plain JSON (see src/app/variants.cpp and
// AppController's doc comments). Times are ms since the epoch; dates are
// ISO yyyy-mm-dd strings.

export type Priority = 'low' | 'medium' | 'high' | 'urgent'
export type Energy = 'low' | 'medium' | 'high' | 'peak'
export type TaskStatus = 'pending' | 'in_progress' | 'completed' | 'cancelled'

export interface Task {
  id: string
  title: string
  description: string
  status: TaskStatus
  priority: Priority
  energy: Energy
  estimate: number
  complexity: number
  dueDate: string
  scheduledTime: string
  fixedTime: boolean
  parentId: string
  categoryId: string
  tags: string[]
  color: string
  isRecurring: boolean
  recurrenceText?: string
  createdAt: number
}

export interface ReadyTask extends Task {
  subtasksDone: number
  subtasksTotal: number
}

export interface AgendaItem {
  kind: 'schedule' | 'occurrence' | 'event'
  state: 'upcoming' | 'now' | 'overdue' | 'done' | 'skipped'
  id: string
  taskId: string
  title: string
  start: number
  end: number
  allDay: boolean
  timeless: boolean
  working: boolean
  startsSoon: boolean
  bumpedCount: number
  color: string
  priority: Priority
  energy: Energy
}

export interface Today {
  date: string
  dateLabel: string
  allDay: AgendaItem[]
  now: AgendaItem[]
  upcoming: AgendaItem[]
  overdue: AgendaItem[]
  done: AgendaItem[]
  ready: ReadyTask[]
  readyTotal: number
  closeOutDue: boolean
  reviewDue: boolean
}

export interface Working {
  id?: string
  taskId?: string
  startedAt?: number
  title?: string
}

export interface EnergyState {
  level?: number
  at?: number
  label?: string
  due?: boolean
}

export interface Away {
  since?: number
  until?: number
  minutes?: number
  title?: string
}

export interface CapturePreview {
  title: string
  tags: string[]
  priority?: Priority
  energy?: Energy
  estimate?: number
  date?: string
  time?: string
  recurrence?: string
  category?: string
  categoryKnown?: boolean
  fromAi?: boolean
}

export interface Category {
  id: string
  name: string
  color: string
  isDefault: boolean
  showOnSchedule: boolean
}

export type Settings = Record<string, unknown>
