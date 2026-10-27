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

export interface CategoryDay {
  day: number // 0 = Sunday
  name?: string
  enabled: boolean
  start: string
  end: string
}

export interface Category {
  id: string
  name: string
  color: string
  isDefault: boolean
  showOnSchedule: boolean
  days?: CategoryDay[]
}

export type Settings = Record<string, unknown>

export interface Recurrence {
  type: 'daily' | 'weekly'
  interval: number
  days?: number[]
}

export interface TaskAction {
  id: string
  kind: 'url' | 'file' | 'app' | 'command'
  target: string
  label: string
  icon: string
  onStart: boolean
  needsApproval: boolean
}

export interface ScheduleBlock {
  id: string
  taskId: string
  start: number
  end: number
  completed: boolean
  bumpedCount: number
  label: string
}

export interface TaskDetail extends Task {
  recurrence?: Recurrence
  subtasks: Task[]
  prerequisites: Task[]
  dependents: Task[]
  parentTitle?: string
  schedules: ScheduleBlock[]
  loggedMinutes: number
  actions: TaskAction[]
  categoryName?: string
  categoryColor?: string
}

// A row of the Tasks list (TaskListModel::rows()).
export interface TaskRowData {
  taskId: string
  title: string
  description: string
  status: TaskStatus
  priority: Priority
  energy: Energy
  estimate: number
  dueDate: string
  dueState: '' | 'overdue' | 'today' | 'soon'
  tags: string[]
  categoryName: string
  categoryColor: string
  isRecurring: boolean
  recurrenceText: string
  blocked: boolean
  subtasksDone: number
  subtasksTotal: number
  parentTitle: string
  nextBlock: number
  loggedMinutes: number
  taskColor: string
}

export interface NoteSummary {
  id: string
  title: string
  snippet: string
  tags: string[]
  color: string
  taskId: string
  taskTitle: string
  updatedAt: number
}

export interface ProposedStep {
  title: string
  minutes: number
  energy: Energy
  checked: boolean
}

export interface AiState {
  provider?: 'off' | 'anthropic' | 'ollama'
  ready?: boolean
  busy?: boolean
  label?: string
}

export interface DayAgenda {
  date: string
  label: string
  dayStart: number
  dayEnd: number
  isToday: boolean
  items: AgendaItem[]
  allDay: AgendaItem[]
  windows: { name: string; color: string; start: number; end: number }[]
}

export interface PlannedBlock {
  taskId: string
  title: string
  start: number
  end: number
  reason: string
  estimate: number
  checked: boolean
}

export interface Plan {
  date: string
  label: string
  blocks: PlannedBlock[]
  unplaced: { taskId: string; title: string; reason: string }[]
  explanation: string
  usedAi: boolean
  aiError: string
  by: string
  logId: string
}

export type BlockingMode = 'none' | 'block_all' | 'recurring_only' | 'tags_only'

export interface EventDetail {
  id: string
  title: string
  description?: string
  allDay: boolean
  date: string
  endDate: string
  startTime: string
  endTime: string
  blockingMode: BlockingMode
  allowedTags: string[]
  color: string
  location: string
  feed?: string
}

export interface NoteDetail {
  id: string
  title: string
  content: string
  tags: string[]
  color: string
  taskId: string
  taskTitle: string
  updatedAt: number
}

export interface AiSettings extends AiState {
  hasKey?: boolean
  model?: string
  ollamaUrl?: string
  ollamaModel?: string
  shareNotes?: boolean
  local?: boolean
}

export interface Feed {
  id: string
  name: string
  url: string
  color: string
  enabled: boolean
  error?: string
  lastSync?: number
  count?: number
  device?: boolean
}

export interface SyncState {
  syncing?: boolean
  feeds?: Feed[]
  linear?: {
    enabled?: boolean
    user?: string
    error?: string
    lastSync?: number
    summary?: string
    teams?: { key: string; name: string }[]
    teamKeys?: string[]
    includeBacklog?: boolean
  }
}

export interface DeviceCalendars {
  supported: boolean
  access: 'granted' | 'notAsked' | 'denied'
  calendars: { id: string; title: string; account: string; color: string; feedId: string }[]
}

export interface RemoteState {
  enabled?: boolean
  running?: boolean
  error?: string
  listen?: string
  publicUrl?: string
  pairUrl?: string
  secure?: boolean
  phones?: { endpoint: string; label: string; addedAt: number }[]
  streams?: number
  serveCommand?: string
  notify?: string
  tailscaleName?: string
}
