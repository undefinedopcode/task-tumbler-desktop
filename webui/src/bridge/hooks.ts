import { useEffect, useState, useSyncExternalStore } from 'react'
import { connection, current, type Objects, type QObj } from './channel'

/* eslint-disable @typescript-eslint/no-explicit-any */

type Name = keyof Objects

function resolveObject(objects: Objects, path: string): QObj | undefined {
  // "app" or "app.tasks" (a QObject-valued property).
  let o: any = objects
  for (const part of path.split('.')) o = o?.[part]
  return o
}

// Calls a method; resolves with what it returns.
export async function call<T = unknown>(path: Name | `${Name}.${string}`, method: string, ...args: unknown[]): Promise<T> {
  const o = resolveObject(await connection, path)
  if (!o || typeof o[method] !== 'function') throw new Error(`no ${path}.${method}`)
  return new Promise<T>((resolve) => o[method](...args, (result: T) => resolve(result)))
}

// One property of a published object, kept current through its change signal.
const stores = new Map<string, { value: unknown; listeners: Set<() => void>; wired: boolean }>()

// Properties whose change signal isn't <name>Changed.
const notifyOf: Record<string, string> = {
  'app#canUndo': 'undoStateChanged',
  'app#canRedo': 'undoStateChanged',
  'app#undoText': 'undoStateChanged',
  'app#redoText': 'undoStateChanged',
  'shell#accent': 'colorSchemeChanged',
  'app.tasks#count': 'countChanged',
  'app.tasks#totalActive': 'countChanged',
}

function store(path: string, prop: string) {
  const key = `${path}#${prop}`
  let s = stores.get(key)
  if (!s) {
    s = { value: undefined, listeners: new Set(), wired: false }
    stores.set(key, s)
  }
  const entry = s
  if (!entry.wired) {
    entry.wired = true
    connection.then((objects) => {
      const o = resolveObject(objects, path)
      if (!o) return
      entry.value = o[prop]
      entry.listeners.forEach((l) => l())
      o[notifyOf[key] ?? `${prop}Changed`]?.connect(() => {
        entry.value = o[prop]
        entry.listeners.forEach((l) => l())
      })
    })
  }
  return entry
}

export function useProp<T>(path: Name | `${Name}.${string}`, prop: string, fallback: T): T {
  const s = store(path, prop)
  const value = useSyncExternalStore(
    (cb) => {
      s.listeners.add(cb)
      return () => s.listeners.delete(cb)
    },
    () => s.value,
  )
  return (value as T | undefined) ?? fallback
}

// Writes a property (one with a WRITE accessor).
export async function setProp(path: Name | `${Name}.${string}`, prop: string, value: unknown) {
  const o = resolveObject(await connection, path)
  if (o) o[prop] = value
}

// Runs `fn` on each emission of a signal while the component is mounted.
export function useSignal(path: Name | `${Name}.${string}`, signal: string, fn: (...args: any[]) => void) {
  useEffect(() => {
    let o: QObj | undefined
    let alive = true
    const handler = (...args: any[]) => fn(...args)
    const hook = (objects: Objects) => {
      if (!alive) return
      o = resolveObject(objects, path)
      o?.[signal]?.connect(handler)
    }
    // Straight away when connected: a request made right after (in a later
    // effect) must not get its answer before this is listening.
    const ready = current()
    if (ready) hook(ready)
    else connection.then(hook)
    return () => {
      alive = false
      o?.[signal]?.disconnect(handler)
    }
  }, [path, signal, fn])
}

// A method's answer, asked again whenever `deps` change or `refreshOn`
// signals fire (e.g. the controller's `changed`).
export function useQuery<T>(
  path: Name | `${Name}.${string}`,
  method: string,
  args: unknown[],
  fallback: T,
  refreshOn: string[] = ['changed'],
): T {
  const [value, setValue] = useState<T>(fallback)
  const key = JSON.stringify(args)
  useEffect(() => {
    let alive = true
    const ask = () => call<T>(path, method, ...args).then((v) => alive && setValue(v))
    ask()
    const unhook: (() => void)[] = []
    connection.then((objects) => {
      const app = resolveObject(objects, path.split('.')[0])
      for (const sig of refreshOn) {
        const s = app?.[sig] ?? resolveObject(objects, path)?.[sig]
        if (!s) continue
        s.connect(ask)
        unhook.push(() => s.disconnect(ask))
      }
    })
    return () => {
      alive = false
      unhook.forEach((u) => u())
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [path, method, key])
  return value
}

export function isReady(): boolean {
  return current() !== null
}
