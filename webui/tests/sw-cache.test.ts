import test from 'node:test';
import assert from 'node:assert/strict';

/**
 * The worker in `public/sw.js` is plain JavaScript that the build copies into
 * `dist/` verbatim. These tests drive it the way a browser does: a fake
 * `self` that collects the listeners, a fake `caches`, and a fake `fetch`.
 *
 * The worker installs its listeners when the module is evaluated, so the fakes
 * have to exist before it is loaded. That is why the import is dynamic.
 */

const ORIGIN = 'https://app.example';
const BASE = `${ORIGIN}/`;

type Listener = (event: WorkerEvent) => void;

interface WorkerEvent {
  request?: Request;
  respondWith?: (response: Promise<Response>) => void;
  waitUntil?: (promise: Promise<unknown>) => void;
}

const listeners = new Map<string, Listener[]>();

const workerGlobal = {
  location: new URL(`${ORIGIN}/sw.js`),
  addEventListener(type: string, handler: Listener) {
    const list = listeners.get(type) ?? [];
    list.push(handler);
    listeners.set(type, list);
  },
  skipWaiting: async () => {},
  clients: { claim: async () => {} },
};

// --- fake cache storage -------------------------------------------------

const cacheStore = new Map<string, Map<string, Response>>();
let openedCacheName = '';
let putFails = false;

function urlOf(target: Request | URL | string): string {
  if (typeof target === 'string') return new URL(target, BASE).href;
  if (target instanceof URL) return target.href;
  return target.url;
}

const cachesFake = {
  async open(name: string) {
    openedCacheName = name;
    let store = cacheStore.get(name);
    if (!store) {
      store = new Map();
      cacheStore.set(name, store);
    }
    return {
      async addAll(urls: string[]) {
        for (const url of urls) {
          const response = await fetchFake(new Request(urlOf(url)));
          if (!response.ok) throw new Error(`precache failed for ${url}`);
          store!.set(urlOf(url), response);
        }
      },
      async put(request: Request | URL | string, response: Response) {
        if (putFails) throw new Error('cache write failed');
        store!.set(urlOf(request), response);
      },
    };
  },
  async keys() {
    return [...cacheStore.keys()];
  },
  async delete(name: string) {
    return cacheStore.delete(name);
  },
  async match(request: Request | URL | string) {
    const url = urlOf(request);
    for (const store of cacheStore.values()) {
      const hit = store.get(url);
      if (hit) return hit;
    }
    return undefined;
  },
};

// --- fake network -------------------------------------------------------

type FetchImpl = (request: Request) => Promise<Response>;

let fetchImpl: FetchImpl = async () => new Response('ok', { status: 200 });

function fetchFake(request: Request): Promise<Response> {
  return fetchImpl(request);
}

// --- wire the fakes into the globals the worker reads --------------------

const globals = globalThis as unknown as Record<string, unknown>;
globals.self = workerGlobal;
globals.caches = cachesFake;
globals.fetch = fetchFake;

await import('../public/sw.js');

// --- helpers -------------------------------------------------------------

function reset() {
  cacheStore.clear();
  putFails = false;
  fetchImpl = async () => new Response('ok', { status: 200 });
}

function dispatch(type: string, event: WorkerEvent): void {
  for (const handler of listeners.get(type) ?? []) handler(event);
}

function handleFetch(request: Request): Promise<Response> | undefined {
  let responded: Promise<Response> | undefined;
  dispatch('fetch', {
    request,
    respondWith: (response) => {
      responded = response;
    },
  });
  return responded;
}

async function fetchThroughWorker(request: Request): Promise<Response> {
  const responded = handleFetch(request);
  assert.ok(responded, 'the worker should answer this request');
  return responded;
}

async function runLifecycleEvent(type: 'install' | 'activate'): Promise<void> {
  const pending: Promise<unknown>[] = [];
  dispatch(type, {
    waitUntil: (promise) => {
      pending.push(promise);
    },
  });
  await Promise.all(pending);
}

async function seed(url: string, response: Response): Promise<void> {
  const cache = await cachesFake.open('seed');
  await cache.put(urlOf(url), response);
}

function isCached(url: string): boolean {
  for (const store of cacheStore.values()) {
    if (store.has(urlOf(url))) return true;
  }
  return false;
}

async function cachedBody(url: string): Promise<string | undefined> {
  for (const store of cacheStore.values()) {
    const hit = store.get(urlOf(url));
    if (hit) return hit.text();
  }
  return undefined;
}

// --- tests ---------------------------------------------------------------

test('a successful same-origin GET is written to the cache', async () => {
  reset();
  fetchImpl = async () => new Response('bundle', { status: 200 });
  const url = `${BASE}assets/index.js`;

  const response = await fetchThroughWorker(new Request(url));

  assert.equal(await response.text(), 'bundle');
  assert.equal(await cachedBody(url), 'bundle');
});

test('a failed fetch falls back to the cached copy', async () => {
  reset();
  const url = `${BASE}assets/index.js`;
  await seed(url, new Response('from cache'));
  fetchImpl = async () => {
    throw new TypeError('network down');
  };

  const response = await fetchThroughWorker(new Request(url));

  assert.equal(await response.text(), 'from cache');
});

test('a failed fetch with nothing cached fails the request', async () => {
  reset();
  fetchImpl = async () => {
    throw new TypeError('network down');
  };

  await assert.rejects(() => fetchThroughWorker(new Request(`${BASE}assets/index.js`)));
});

test('a response that is not ok is not written to the cache', async () => {
  reset();
  fetchImpl = async () => new Response('missing', { status: 404 });
  const url = `${BASE}assets/index.js`;

  const response = await fetchThroughWorker(new Request(url));

  assert.equal(response.status, 404);
  assert.equal(isCached(url), false);
});

test('a failing cache write still returns the response', async () => {
  reset();
  putFails = true;
  fetchImpl = async () => new Response('bundle', { status: 200 });

  const response = await fetchThroughWorker(new Request(`${BASE}assets/index.js`));

  assert.equal(await response.text(), 'bundle');
});

test('a request that is not a GET is left alone', async () => {
  reset();
  let fetched = false;
  fetchImpl = async () => {
    fetched = true;
    return new Response('ok', { status: 200 });
  };

  const responded = handleFetch(new Request(`${BASE}api/send`, { method: 'POST' }));

  assert.equal(responded, undefined);
  assert.equal(fetched, false);
});

test('a cross-origin request is left alone', async () => {
  reset();
  let fetched = false;
  fetchImpl = async () => {
    fetched = true;
    return new Response('ok', { status: 200 });
  };

  const responded = handleFetch(new Request('https://other.example/lib.js'));

  assert.equal(responded, undefined);
  assert.equal(fetched, false);
});

test('installing the worker precaches the shell, the manifest and the icon', async () => {
  reset();

  await runLifecycleEvent('install');

  const stored = [...cacheStore.values()].flatMap((store) => [...store.keys()]).sort();
  assert.deepEqual(stored, [
    `${ORIGIN}/`,
    `${ORIGIN}/favicon.svg`,
    `${ORIGIN}/manifest.json`,
  ]);
});

test('activating drops the caches the earlier workers wrote', async () => {
  reset();
  fetchImpl = async () => new Response('bundle', { status: 200 });
  await fetchThroughWorker(new Request(`${BASE}assets/index.js`));
  const current = openedCacheName;
  cacheStore.set('dorky-v1', new Map());
  cacheStore.set('dorky-v2', new Map());

  await runLifecycleEvent('activate');

  assert.deepEqual([...cacheStore.keys()], [current]);
});
