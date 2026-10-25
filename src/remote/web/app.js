// Task Tumbler on the phone: a remote control for the desktop app over the
// tailnet. Holds no data of its own beyond the pairing token and an outbox of
// captures made while the computer was out of reach.
'use strict';

const $ = (id) => document.getElementById(id);
const timeFmt = new Intl.DateTimeFormat(undefined, { hour: 'numeric', minute: '2-digit' });
const fmtTime = (ms) => timeFmt.format(new Date(ms));
const dayFmt = new Intl.DateTimeFormat(undefined, { weekday: 'short', day: 'numeric', month: 'short' });
// "today", "tomorrow", "yesterday" or "Fri 30 Oct", from a local YYYY-MM-DD.
function dueLabel(iso) {
  const [y, m, d] = iso.split('-').map(Number);
  const due = new Date(y, m - 1, d);
  const today = new Date(); today.setHours(0, 0, 0, 0);
  const days = Math.round((due - today) / 86400000);
  return { 0: 'today', 1: 'tomorrow', '-1': 'yesterday' }[days] || dayFmt.format(due);
}

// ---- tiny IndexedDB key/value store (shared with the service worker) ----
const db = (() => {
  let opening;
  const open = () => opening ||= new Promise((resolve, reject) => {
    const req = indexedDB.open('tumbler', 1);
    req.onupgradeneeded = () => { req.result.createObjectStore('kv'); req.result.createObjectStore('outbox', { keyPath: 'clientId' }); };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  const tx = async (store, mode, fn) => {
    const d = await open();
    return new Promise((resolve, reject) => {
      const t = d.transaction(store, mode);
      const r = fn(t.objectStore(store));
      t.oncomplete = () => resolve(r && r.result);
      t.onerror = () => reject(t.error);
    });
  };
  return {
    get: (k) => tx('kv', 'readonly', (s) => s.get(k)),
    set: (k, v) => tx('kv', 'readwrite', (s) => s.put(v, k)),
    del: (k) => tx('kv', 'readwrite', (s) => s.delete(k)),
    queue: (item) => tx('outbox', 'readwrite', (s) => s.put(item)),
    queued: () => tx('outbox', 'readonly', (s) => s.getAll()),
    unqueue: (id) => tx('outbox', 'readwrite', (s) => s.delete(id)),
  };
})();

// ---- pairing ----
let token = localStorage.getItem('tt-token') || '';
if (location.hash.startsWith('#pair=')) {
  token = decodeURIComponent(location.hash.slice(6));
  localStorage.setItem('tt-token', token);
  // The token never stays in the address bar or history.
  history.replaceState(null, '', location.pathname);
}
db.set('token', token).catch(() => {});

class NotPaired extends Error {}

async function api(path, { method = 'GET', body } = {}) {
  const res = await fetch(path, {
    method,
    headers: { Authorization: 'Bearer ' + token, ...(body ? { 'Content-Type': 'application/json' } : {}) },
    body: body ? JSON.stringify(body) : undefined,
    cache: 'no-store',
  });
  if (res.status === 401) throw new NotPaired();
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || res.statusText);
  return data;
}

// ---- state & rendering ----
let snap = null;
let timerHandle = 0;

function setLink(state) {
  $('link').className = 'link ' + state;
  $('link').title = { live: 'Connected', down: 'Computer not reachable', '': 'Connecting…' }[state] || '';
}

function banner(text) {
  $('banner').textContent = text || '';
  $('banner').hidden = !text;
}

function el(tag, attrs = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'color') { if (v) e.style.background = v; } // CSSOM: allowed by the CSP, unlike style=""
    else e.setAttribute(k, v);
  }
  for (const c of children.flat()) if (c != null) e.append(c);
  return e;
}

function button(label, command, { primary = false } = {}) {
  return el('button', {
    class: primary ? 'primary' : '',
    onclick: async (ev) => {
      ev.currentTarget.disabled = true;
      try { await run(command); } finally { ev.currentTarget && (ev.currentTarget.disabled = false); }
    },
  }, label);
}

function agendaItem(item) {
  const finished = item.state === 'done' || item.state === 'skipped';
  const when = item.timeless ? el('div', { class: 'when' }, 'Any time')
    : el('div', { class: 'when' }, fmtTime(item.start), el('div', { class: 'end' }, fmtTime(item.end)));
  const sub = [];
  if (item.kind === 'occurrence') sub.push('Repeats');
  if (item.kind === 'event') sub.push('Event');
  if (item.working) sub.push('Working on it');
  if (item.state === 'overdue') sub.push('Missed');
  if (item.bumpedCount) sub.push(`Moved ${item.bumpedCount}×`);
  const actions = [];
  if (!finished && item.kind === 'schedule') {
    if (!item.working) actions.push(button('Start', { type: 'startWork', taskId: item.taskId }));
    actions.push(item.state === 'overdue' ? button('Later', { type: 'bumpNext', id: item.id }) : button('+15', { type: 'bump', id: item.id, minutes: 15 }));
    actions.push(button('Done', { type: 'completeSchedule', id: item.id }, { primary: true }));
  } else if (!finished && item.kind === 'occurrence') {
    actions.push(button('Skip', { type: 'occurrence', id: item.id, status: 'skipped' }));
    actions.push(button('Done', { type: 'occurrence', id: item.id, status: 'completed' }, { primary: true }));
  }
  return el('div', { class: `item ${item.state}${finished ? ' done' : ''}` },
    el('div', { class: 'bar', color: item.color || '' }),
    when,
    el('div', { class: 'what' }, el('div', { class: 'title' }, item.title), sub.length ? el('div', { class: 'sub' }, sub.join(' · ')) : null),
    el('div', { class: 'actions' }, actions));
}

function readyItem(task) {
  const sub = [`${task.estimate} min`];
  if (task.dueDate) sub.push('due ' + dueLabel(task.dueDate));
  if (task.status === 'in_progress') sub.unshift('In progress');
  return el('div', { class: 'item' },
    el('div', { class: 'bar' }),
    el('div', { class: 'what' }, el('div', { class: 'title' }, task.title), el('div', { class: 'sub' }, sub.join(' · '))),
    el('div', { class: 'actions' },
      button('Start', { type: 'startWork', taskId: task.id }),
      button('Plan', { type: 'scheduleNext', taskId: task.id })));
}

function section(title, nodes) {
  if (!nodes.length) return null;
  return el('section', {}, el('h2', {}, title), el('div', { class: 'card' }, nodes));
}

function render() {
  const t = snap.today;
  $('date').textContent = t.dateLabel || '';
  const w = snap.working || {};
  $('working').hidden = !w.taskId;
  if (w.taskId) $('workingTitle').textContent = w.title;
  clearInterval(timerHandle);
  const tick = () => {
    if (!w.startedAt) return;
    const s = Math.max(0, Math.floor((Date.now() - w.startedAt) / 1000));
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
    $('workingTimer').textContent = (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(sec).padStart(2, '0');
  };
  tick();
  if (w.startedAt) timerHandle = setInterval(tick, 1000);

  $('energy').hidden = !(snap.energy && snap.energy.due) || !!w.taskId;

  const lists = $('lists');
  lists.replaceChildren(...[
    section('Right now', t.now.map(agendaItem)),
    section("Didn't happen yet", t.overdue.map(agendaItem)),
    section('Coming up', t.upcoming.map(agendaItem)),
    section('Ready to work', t.ready.map(readyItem)),
    t.done.length ? el('p', { class: 'muted small' }, `${t.done.length} done today`) : null,
  ].filter(Boolean));
  if (!lists.childElementCount && !w.taskId) lists.append(el('div', { class: 'empty' }, 'A clear day. Capture anything below.'));
}

async function refresh() {
  if (!token) return showPair();
  try {
    snap = await api('/api/snapshot');
    $('pair').hidden = true;
    render();
    setLink('live');
    flushOutbox();
  } catch (e) {
    if (e instanceof NotPaired) return showPair();
    setLink('down');
  }
}

function showPair() {
  $('pair').hidden = false;
  $('lists').replaceChildren();
  $('working').hidden = true;
  $('energy').hidden = true;
}

async function run(command) {
  try {
    await api('/api/command', { method: 'POST', body: command });
    await refresh();
  } catch (e) {
    if (e instanceof NotPaired) return showPair();
    banner(navigator.onLine ? `Couldn't do that: ${e.message}` : 'Offline: try again when connected.');
    setTimeout(() => banner(''), 4000);
  }
}

// ---- live updates: a streamed /api/events (fetch, so the token can go in a header) ----
let stream = null;
let backoff = 1000;

async function listen() {
  if (!token || stream || document.hidden) return;
  const ctrl = new AbortController();
  stream = ctrl;
  try {
    const res = await fetch('/api/events', { headers: { Authorization: 'Bearer ' + token }, signal: ctrl.signal, cache: 'no-store' });
    if (res.status === 401) { stream = null; return showPair(); }
    if (!res.ok || !res.body) throw new Error('stream refused');
    setLink('live');
    backoff = 1000;
    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';
    let pending = 0;
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true });
      let cut;
      while ((cut = buf.indexOf('\n\n')) >= 0) {
        const chunk = buf.slice(0, cut);
        buf = buf.slice(cut + 2);
        if (/^event: changed/m.test(chunk)) {
          clearTimeout(pending);
          pending = setTimeout(refresh, 250); // coalesce bursts
        }
      }
    }
  } catch (e) {
    if (ctrl.signal.aborted) return;
  } finally {
    if (stream === ctrl) stream = null;
  }
  setLink('down');
  setTimeout(listen, backoff);
  backoff = Math.min(backoff * 2, 30000);
}

document.addEventListener('visibilitychange', () => {
  if (document.hidden) { stream && stream.abort(); stream = null; }
  else { refresh(); listen(); }
});
window.addEventListener('online', () => { refresh(); listen(); });

// ---- capture, with an outbox for when the computer is asleep ----
let previewTimer = 0;
$('captureText').addEventListener('input', () => {
  clearTimeout(previewTimer);
  const text = $('captureText').value.trim();
  if (!text) return $('preview').replaceChildren();
  previewTimer = setTimeout(async () => {
    try {
      const p = await api('/api/preview', { method: 'POST', body: { text } });
      const chips = [p.title, p.date, p.time, p.recurrence, p.estimate && `${p.estimate} min`, p.priority, ...(p.tags || []).map((t) => '#' + t)];
      $('preview').replaceChildren(...chips.filter(Boolean).map((c) => el('span', { class: 'chip' }, c)));
    } catch { $('preview').replaceChildren(); }
  }, 250);
});

$('capture').addEventListener('submit', async (ev) => {
  ev.preventDefault();
  const text = $('captureText').value.trim();
  if (!text) return;
  const item = { clientId: crypto.randomUUID(), text, at: Date.now() };
  $('captureText').value = '';
  $('preview').replaceChildren();
  try {
    await api('/api/command', { method: 'POST', body: { type: 'capture', text, clientId: item.clientId } });
    await refresh();
  } catch (e) {
    if (e instanceof NotPaired) return showPair();
    await db.queue(item);
    banner('Saved on the phone. It will be added when the computer is reachable.');
  }
});

async function flushOutbox() {
  let items = [];
  try { items = await db.queued(); } catch { return; }
  for (const item of items.sort((a, b) => a.at - b.at)) {
    try {
      // clientId makes a retry harmless if the first attempt did get through.
      await api('/api/command', { method: 'POST', body: { type: 'capture', text: item.text, clientId: item.clientId } });
      await db.unqueue(item.clientId);
    } catch { return; }
  }
  if (items.length) { banner(''); refresh(); }
}

// ---- energy ----
document.querySelectorAll('[data-energy]').forEach((b) =>
  b.addEventListener('click', () => run({ type: 'energy', level: Number(b.dataset.energy) })));
document.querySelector('[data-cmd="done-working"]').addEventListener('click', () => snap && snap.working && run({ type: 'completeTask', taskId: snap.working.taskId }));
document.querySelector('[data-cmd="stop"]').addEventListener('click', () => run({ type: 'stopWork' }));

// ---- notifications (Web Push) ----
const pushSupported = 'serviceWorker' in navigator && 'PushManager' in window && window.isSecureContext;

async function pushStatus() {
  if (!pushSupported) {
    $('pushState').textContent = window.isSecureContext ? 'This browser can\'t receive notifications.' : 'Notifications need the https:// tailnet address.';
    $('pushButton').hidden = true;
    return;
  }
  const reg = await navigator.serviceWorker.ready;
  const sub = await reg.pushManager.getSubscription();
  $('pushState').textContent = sub ? 'Notifications are on for this phone.' : 'Get reminders here when you\'re away from the computer.';
  $('pushButton').textContent = sub ? 'Turn off notifications' : 'Turn on notifications';
}

$('pushButton').addEventListener('click', async () => {
  const reg = await navigator.serviceWorker.ready;
  const existing = await reg.pushManager.getSubscription();
  try {
    if (existing) {
      await api('/api/push', { method: 'DELETE', body: { endpoint: existing.endpoint } }).catch(() => {});
      await existing.unsubscribe();
    } else {
      if ((await Notification.requestPermission()) !== 'granted') throw new Error('notifications were not allowed');
      const { publicKey } = await api('/api/vapid');
      const key = Uint8Array.from(atob(publicKey.replace(/-/g, '+').replace(/_/g, '/')), (c) => c.charCodeAt(0));
      const sub = await reg.pushManager.subscribe({ userVisibleOnly: true, applicationServerKey: key });
      await api('/api/push', { method: 'POST', body: { ...sub.toJSON(), label: navigator.userAgentData?.platform || 'Phone' } });
    }
  } catch (e) {
    banner(`Notifications: ${e.message}`);
  }
  pushStatus();
});

$('menuButton').addEventListener('click', () => { pushStatus(); $('menu').showModal(); });
$('closeMenu').addEventListener('click', () => $('menu').close());
$('refreshButton').addEventListener('click', () => { $('menu').close(); refresh(); });
$('unpairButton').addEventListener('click', async () => {
  try {
    const reg = await navigator.serviceWorker.ready;
    const sub = await reg.pushManager.getSubscription();
    if (sub) { await api('/api/push', { method: 'DELETE', body: { endpoint: sub.endpoint } }).catch(() => {}); await sub.unsubscribe(); }
  } catch {}
  token = '';
  localStorage.removeItem('tt-token');
  await db.del('token').catch(() => {});
  $('menu').close();
  showPair();
});

if ('serviceWorker' in navigator && window.isSecureContext) {
  navigator.serviceWorker.register('/sw.js');
  navigator.serviceWorker.addEventListener('message', (e) => { if (e.data && e.data.type === 'push') refresh(); });
}

refresh();
listen();
