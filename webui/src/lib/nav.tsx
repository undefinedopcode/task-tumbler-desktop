import { createContext, useContext } from 'react'

// Opening things from anywhere: a task's detail panel, a note, a page.
export interface Nav {
  openTask: (id: string) => void
  openNote: (id: string) => void
  showPage: (page: string) => void
  // "Plan my day" for a date (default today).
  openPlan: (isoDate?: string) => void
  // Edit an event, or a new one ("" id) starting at `start` ms.
  editEvent: (id: string, start?: number) => void
}

export const NavContext = createContext<Nav>({ openTask: () => {}, openNote: () => {}, showPage: () => {}, openPlan: () => {}, editEvent: () => {} })

export function useNav(): Nav {
  return useContext(NavContext)
}
