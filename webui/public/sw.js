// Minimal service worker — existence enables PWA installability.
// No aggressive caching; the app needs BLE which requires HTTPS anyway.
const CACHE = 'dorky-v1';
const PRECACHE = ['/', '/manifest.json', '/favicon.svg'];

self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE).then((c) => c.addAll(PRECACHE)).then(() => self.skipWaiting()),
  );
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))),
    ).then(() => self.clients.claim()),
  );
});

self.addEventListener('fetch', (e) => {
  // Navigation requests: network-first so updates land immediately.
  if (e.request.mode === 'navigate') {
    e.respondWith(
      fetch(e.request).catch(() => caches.match('/') ),
    );
    return;
  }
  // Assets: cache-first.
  e.respondWith(
    caches.match(e.request).then((hit) => hit ?? fetch(e.request)),
  );
});
