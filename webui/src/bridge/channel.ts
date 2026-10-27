// The connection to the app. Inside Task Tumbler the page talks to the C++
// objects over a QWebChannel; in a plain browser (npm run dev) it gets a
// mock with sample data, so the UI can be worked on without the app.

/* eslint-disable @typescript-eslint/no-explicit-any */

export interface Signal {
  connect(fn: (...args: any[]) => void): void
  disconnect(fn: (...args: any[]) => void): void
}

// A published QObject as qwebchannel.js presents it: properties as fields,
// <name>Changed signals, and methods that answer through a trailing callback.
export type QObj = Record<string, any>

export interface Objects {
  app: QObj
  shell: QObj
  sync?: QObj
  remote?: QObj
}

declare global {
  interface Window {
    qt?: { webChannelTransport: unknown }
    QWebChannel?: new (transport: unknown, ready: (channel: { objects: Objects }) => void) => unknown
    tumbler?: { showPage: (name: string) => void; openTask: (id: string) => void; openPlan: (day?: string) => void }
  }
}

export const inApp = typeof window !== 'undefined' && !!window.qt?.webChannelTransport && !!window.QWebChannel

export const connection: Promise<Objects> = inApp
  ? new Promise((resolve) => {
      new window.QWebChannel!(window.qt!.webChannelTransport, (channel) => resolve(channel.objects))
    })
  : import('./mock').then((m) => m.createMock())

let objects: Objects | null = null
connection.then((o) => (objects = o))

export function current(): Objects | null {
  return objects
}
