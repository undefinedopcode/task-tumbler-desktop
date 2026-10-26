// Service worker: keeps the app shell available offline and turns pushes
// into notifications whose buttons act without opening the app.
'use strict';

const SHELL = 'tumbler-shell-v2';
const FILES = ['/', '/index.html', '/app.js', '/app.css', '/icon.svg', '/manifest.webmanifest'];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(SHELL).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k !== SHELL).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

// The app shell: network first so updates arrive, cache when offline.
// The API is never cached.
self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET' || url.origin !== location.origin || url.pathname.startsWith('/api/')) return;
  e.respondWith(fetch(e.request)
    .then((res) => { const copy = res.clone(); caches.open(SHELL).then((c) => c.put(e.request, copy)); return res; })
    .catch(() => caches.match(e.request).then((r) => r || caches.match('/index.html'))));
});

function token() {
  return new Promise((resolve) => {
    const req = indexedDB.open('tumbler', 1);
    req.onupgradeneeded = () => { req.result.createObjectStore('kv'); req.result.createObjectStore('outbox', { keyPath: 'clientId' }); };
    req.onsuccess = () => {
      const get = req.result.transaction('kv').objectStore('kv').get('token');
      get.onsuccess = () => resolve(get.result || '');
      get.onerror = () => resolve('');
    };
    req.onerror = () => resolve('');
  });
}

self.addEventListener('push', (e) => {
  const payload = e.data ? e.data.json() : {};
  e.waitUntil((async () => {
    const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    const visible = windows.find((w) => w.visibilityState === 'visible');
    // The app is open and on screen: it shows the change itself.
    if (visible) { visible.postMessage({ type: 'push', payload }); return; }
    await self.registration.showNotification(payload.title || 'Task Tumbler', {
      body: payload.body || '',
      tag: payload.tag,
      renotify: !!payload.tag,
      icon: '/icon.svg',
      badge: '/icon.svg',
      actions: (payload.actions || []).slice(0, 2).map((a) => ({ action: a.action, title: a.title })),
      data: payload,
    });
  })());
});

self.addEventListener('notificationclick', (e) => {
  const data = e.notification.data || {};
  const command = e.action && data.commands ? data.commands[e.action] : null;
  e.notification.close();
  e.waitUntil((async () => {
    if (command) {
      const t = await token();
      const res = await fetch('/api/command', {
        method: 'POST',
        headers: { Authorization: 'Bearer ' + t, 'Content-Type': 'application/json' },
        body: JSON.stringify(command),
      }).catch(() => null);
      if (res && res.ok) return;
    }
    // Some notifications open a view: the evening one opens #closeout.
    const url = data.url || '/';
    const hash = url.includes('#') ? url.slice(url.indexOf('#') + 1) : '';
    const windows = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
    if (windows.length) {
      windows[0].postMessage({ type: 'open', hash });
      return windows[0].focus();
    }
    return self.clients.openWindow(url);
  })());
});
