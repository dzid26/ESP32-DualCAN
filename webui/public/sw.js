// Minimal service worker — existence enables PWA installability.
// Network-first for everything: a cached shell or asset must never outlive
// the network copy, otherwise a restart or redeploy leaves the app unstyled
// until the user clears site data.
const CACHE = 'dorky-v2';
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
  if (e.request.method !== 'GET') return;
  // Network first; cache only as an offline fallback.
  e.respondWith(
    fetch(e.request).catch(() => caches.match(e.request)),
  );
});
