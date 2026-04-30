<script lang="ts">
  import type { Transport } from '../transport/types';
  import type { Protocol, SignalValue } from '../transport/protocol';
  import { onMount, onDestroy } from 'svelte';

  let { transport: _transport, proto, connected }: { transport: Transport; proto: Protocol; connected: boolean } = $props();

  // ---- Actions ----
  let actions = $state<string[]>([]);
  let busy = $state<string | null>(null);
  let actionError = $state<string | null>(null);

  async function refreshActions() {
    if (!connected) return;
    try {
      const r = await proto.listActions();
      actions = r.actions ?? [];
      actionError = null;
    } catch (e: any) {
      actionError = e.message ?? String(e);
    }
  }

  async function invoke(name: string) {
    busy = name;
    actionError = null;
    try {
      await proto.invokeAction(name);
    } catch (e: any) {
      actionError = e.message ?? String(e);
    } finally {
      busy = null;
    }
  }

  // ---- Signal Watch ----
  const LS_KEY = 'dorky-watched-signals';

  interface WatchEntry {
    name: string;
    bus: number;
    state: SignalValue | null;
    error: boolean;
  }

  let watched = $state<WatchEntry[]>(loadWatched());
  let signalInput = $state('');
  let pollTimer: ReturnType<typeof setInterval> | null = null;

  function loadWatched(): WatchEntry[] {
    try {
      const raw = localStorage.getItem(LS_KEY);
      if (!raw) return [];
      return (JSON.parse(raw) as { name: string; bus: number }[]).map(s => ({
        ...s, state: null, error: false,
      }));
    } catch { return []; }
  }

  function saveWatched() {
    localStorage.setItem(LS_KEY, JSON.stringify(watched.map(w => ({ name: w.name, bus: w.bus }))));
  }

  function addSignal() {
    const name = signalInput.trim();
    if (!name || watched.some(w => w.name === name)) return;
    watched = [...watched, { name, bus: 0, state: null, error: false }];
    signalInput = '';
    saveWatched();
  }

  function removeSignal(idx: number) {
    watched = watched.filter((_, i) => i !== idx);
    saveWatched();
  }

  async function pollSignals() {
    if (!connected || watched.length === 0) return;
    const next = await Promise.all(
      watched.map(async w => {
        try {
          const s = await proto.getSignalValue(w.name, w.bus);
          return { ...w, state: s, error: false };
        } catch {
          return { ...w, error: true };
        }
      })
    );
    watched = next;
  }

  function startPoll() {
    if (pollTimer) return;
    pollTimer = setInterval(pollSignals, 1000);
    pollSignals();
  }

  function stopPoll() {
    if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
  }

  // ---- Log ----
  let logs = $state<string[]>([]);
  let logEl = $state<HTMLElement | undefined>(undefined);

  function pushLog(msg: string) {
    logs = [...logs.slice(-199), msg];
  }

  function clearLogs() { logs = []; }

  // ---- Lifecycle ----
  onMount(() => {
    proto.onLog(pushLog);
    refreshActions();
  });

  onDestroy(() => {
    proto.onLog(() => {});
    stopPoll();
  });

  $effect(() => {
    if (connected) {
      refreshActions();
      startPoll();
    } else {
      stopPoll();
    }
  });

  // Auto-scroll log to bottom when new lines arrive.
  $effect(() => {
    logs;
    if (logEl) logEl.scrollTop = logEl.scrollHeight;
  });
</script>

<div class="dashboard">
  <h2>Dashboard</h2>

  {#if !connected}
    <div class="placeholder">
      Connect to the device via BLE to see live data.
    </div>
  {:else}

    <!-- Actions -->
    <section>
      <header>
        <h3>Actions</h3>
        <button onclick={refreshActions}>refresh</button>
      </header>
      {#if actionError}
        <div class="err">{actionError}</div>
      {/if}
      {#if actions.length === 0}
        <div class="placeholder">
          No actions registered. Enable a script that calls
          <code>action_register(name, fn)</code>.
        </div>
      {:else}
        <div class="tiles">
          {#each actions as name}
            <button class="tile" disabled={busy !== null} onclick={() => invoke(name)}>
              {busy === name ? '…' : name}
            </button>
          {/each}
        </div>
      {/if}
    </section>

    <!-- Signal Watch -->
    <section>
      <header>
        <h3>Signal Watch</h3>
      </header>
      <div class="signal-add">
        <input
          bind:value={signalInput}
          placeholder="signal name"
          onkeydown={(e) => e.key === 'Enter' && addSignal()}
        />
        <button onclick={addSignal}>Watch</button>
      </div>
      {#if watched.length > 0}
        <table class="signal-table">
          <thead><tr><th>Signal</th><th>Value</th><th>Prev</th><th>Chg</th><th></th></tr></thead>
          <tbody>
            {#each watched as w, i}
              <tr class:changed={w.state?.changed} class:missing={w.state === null && !w.error}>
                <td class="sig-name">{w.name}</td>
                {#if w.error}
                  <td colspan="3" class="err-cell">error</td>
                {:else if w.state === null}
                  <td colspan="3" class="dim">—</td>
                {:else}
                  <td>{w.state.value}</td>
                  <td class="dim">{w.state.prev}</td>
                  <td class="chg-dot">{w.state.changed ? '●' : ''}</td>
                {/if}
                <td><button class="rm" onclick={() => removeSignal(i)}>×</button></td>
              </tr>
            {/each}
          </tbody>
        </table>
      {:else}
        <div class="placeholder small">
          Enter a signal name from the loaded DBC and press Watch.
        </div>
      {/if}
    </section>

    <!-- Log -->
    <section>
      <header>
        <h3>Log</h3>
        {#if logs.length > 0}
          <button onclick={clearLogs}>clear</button>
        {/if}
      </header>
      {#if logs.length === 0}
        <div class="placeholder small">Berry <code>print()</code> output will appear here.</div>
      {:else}
        <div class="log-panel" bind:this={logEl}>
          {#each logs as line}
            <div class="log-line">{line}</div>
          {/each}
        </div>
      {/if}
    </section>

  {/if}
</div>

<style>
  .placeholder {
    background: #0d0d1a;
    border: 1px dashed #333;
    border-radius: 4px;
    padding: 2rem;
    text-align: center;
    color: #666;
    margin-top: 1rem;
  }
  .placeholder.small { padding: 0.75rem 1rem; }
  header {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-top: 1.25rem;
  }
  section { margin-bottom: 0.5rem; }

  /* Action tiles */
  .tiles {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(140px, 1fr));
    gap: 0.5rem;
    margin-top: 0.5rem;
  }
  .tile {
    padding: 1rem;
    background: #1a1a2e;
    border: 1px solid #333;
    border-radius: 4px;
    color: #ddd;
    cursor: pointer;
    font-size: 0.9rem;
  }
  .tile:hover:not(:disabled) { border-color: #555; background: #222238; }
  .err { color: #f55; margin-top: 0.5rem; }

  /* Signal watch */
  .signal-add {
    display: flex;
    gap: 0.4rem;
    margin-top: 0.5rem;
  }
  .signal-add input {
    flex: 1;
    background: #0d0d1a;
    border: 1px solid #333;
    border-radius: 4px;
    color: #ddd;
    padding: 0.35rem 0.6rem;
    font-size: 0.85rem;
  }
  .signal-table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 0.5rem;
    font-size: 0.85rem;
  }
  .signal-table th, .signal-table td {
    padding: 0.3rem 0.5rem;
    border-bottom: 1px solid #222;
    text-align: left;
  }
  .signal-table th { color: #777; font-weight: normal; }
  .sig-name { font-family: monospace; color: #aad; }
  .dim { color: #666; }
  .chg-dot { color: #5cf; text-align: center; }
  tr.changed td { background: #0d1a1a; }
  .err-cell { color: #f55; }
  .rm {
    background: none;
    border: none;
    color: #555;
    cursor: pointer;
    padding: 0 0.25rem;
    font-size: 1rem;
    line-height: 1;
  }
  .rm:hover { color: #f55; }

  /* Log panel */
  .log-panel {
    background: #050510;
    border: 1px solid #222;
    border-radius: 4px;
    padding: 0.5rem 0.75rem;
    margin-top: 0.5rem;
    max-height: 200px;
    overflow-y: auto;
    font-family: monospace;
    font-size: 0.8rem;
    color: #9f9;
  }
  .log-line { white-space: pre-wrap; line-height: 1.4; }
</style>
