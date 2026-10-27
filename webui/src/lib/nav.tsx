import { createContext, useContext } from 'react'

// Opening things from anywhere: a task's detail panel, a note, a page.
export interface Nav {
  openTask: (id: string) => void
  openNote: (id: string) => void
  showPage: (page: string) => void
}

export const NavContext = createContext<Nav>({ openTask: () => {}, openNote: () => {}, showPage: () => {} })

export function useNav(): Nav {
  return useContext(NavContext)
}
