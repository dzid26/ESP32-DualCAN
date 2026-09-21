// Shared Node-test shims for modules that expect a browser-ish environment.
//
// The repo's test runner is plain `node --test` (no jsdom / vitest), so anything
// imported under test that reads `localStorage` or uses Svelte 5 runes needs a
// minimal shim. Import this file before importing such a module.

interface LocalStorageLike {
  getItem(k: string): string | null;
  setItem(k: string, v: unknown): void;
  removeItem(k: string): void;
  clear(): void;
  key(i: number): string | null;
  readonly length: number;
}

// The shim targets globals that are not declared in this (non-DOM, non-Svelte)
// compilation context, so reach them through a locally-typed view.
const g = globalThis as unknown as {
  localStorage?: LocalStorageLike;
  $state?: unknown;
  $derived?: unknown;
  $effect?: unknown;
};

if (!g.localStorage) {
  const store = new Map<string, string>();
  g.localStorage = {
    getItem: (k) => (store.has(k) ? store.get(k)! : null),
    setItem: (k, v) => void store.set(k, String(v)),
    removeItem: (k) => void store.delete(k),
    clear: () => store.clear(),
    key: (i) => [...store.keys()][i] ?? null,
    get length() { return store.size; },
  };
}

// Svelte 5 runes are compile-time macros. In plain Node they appear as free
// variables; shim them to identity/plain values so logic-only tests can import
// `.svelte.ts` modules without running the Svelte compiler.
if (!g.$state) {
  g.$state = (v: unknown) => v;
  g.$derived = (fn: unknown) => (typeof fn === 'function' ? (fn as () => unknown)() : fn);
  g.$effect = (fn: () => void) => { try { fn(); } catch { /* ignore */ } };
}
