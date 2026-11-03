// Minimal service worker — existence enables PWA installability.
// Network-first for everything: a cached shell or asset must never outlive
// the network copy, otherwise a restart or redeploy leaves the app unstyled
// until the user clears site data.
//
// A successful response is saved, so a start with no connection finds the
// bundles and not just the precached shell.
const CACHE = 'dorky-v3';
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
  const request = e.request;
  if (request.method !== 'GET') return;
  // Cross-origin traffic is none of the worker's business: an opaque
  // response cannot be stored, and intercepting it only risks breaking a
  // request the page is entitled to make.
  if (new URL(request.url).origin !== self.location.origin) return;
  e.respondWith(networkFirst(request));
});

async function networkFirst(request) {
  let response;
  try {
    response = await fetch(request);
  } catch (err) {
    const cached = await caches.match(request);
    if (!cached) throw err;
    return cached;
  }
  if (response.ok) {
    try {
      const cache = await caches.open(CACHE);
      await cache.put(request, response.clone());
    } catch {
      // A full or blocked cache must not take the response down with it.
    }
  }
  return response;
}
