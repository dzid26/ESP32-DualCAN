<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import type { SignalUpdate, SignalValue } from '../../transport/protocol';
  import { dbcStore } from '../../dbcStore.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { onMount, onDestroy, untrack } from 'svelte';

  const LS_KEY = 'dorky-watched-signals';
  const SPARK_LEN = 60;

  type Watch = {
    name: string;
    bus: number;
    state: SignalValue | null;
    error: string | null;
    history: number[];
  };

  let watched = $state<Watch[]>(loadWatched());
  let signalInput = $state('');
  let busInput = $state(0);
  let listError = $state<string | null>(null);

  function loadWatched(): Watch[] {
    try {
      const raw = localStorage.getItem(LS_KEY);
      if (!raw) return [];
      const arr = JSON.parse(raw) as { name: string; bus: number }[];
      return arr.map(w => ({ ...w, state: null, error: null, history: [] }));
    } catch { return []; }
  }
  function saveWatched(): void {
    try {
      localStorage.setItem(LS_KEY, JSON.stringify(watched.map(w => ({ name: w.name, bus: w.bus }))));
    } catch { /* ignore */ }
  }

  // Default the bus selector to the last DBC upload's bus.
  $effect(() => {
    if (dbcStore.lastUploadedBus >= 0) busInput = dbcStore.lastUploadedBus;
  });

  async function seedValue(name: string, bus: number): Promise<void> {
    try {
      const s = await app.proto.getSignalValue(name, bus);
      watched = watched.map(w =>
        w.name === name && w.bus === bus
          ? { ...w, state: s, error: null, history: s ? [s.value] : [] }
          : w);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      watched = watched.map(w =>
        w.name === name && w.bus === bus ? { ...w, error: msg } : w);
    }
  }

  async function add(): Promise<void> {
    const name = signalInput.trim();
    if (!name) return;
    if (watched.some(w => w.name === name && w.bus === busInput)) return;
    const bus = busInput;
    watched = [...watched, { name, bus, state: null, error: null, history: [] }];
    signalInput = '';
    saveWatched();
    if (app.connected) {
      try { await app.proto.subscribeSignal(name, bus); } catch { /* ignore */ }
      void seedValue(name, bus);
    }
  }

  async function remove(idx: number): Promise<void> {
    const w = watched[idx];
    watched = watched.filter((_, i) => i !== idx);
    saveWatched();
    if (app.connected && w) {
      try { await app.proto.unsubscribeSignal(w.name, w.bus); } catch { /* ignore */ }
    }
  }

  function onPush(s: SignalUpdate): void {
    watched = watched.map(w => {
      if (w.name !== s.name || w.bus !== s.bus) return w;
      const history = [...w.history, s.value].slice(-SPARK_LEN);
      const state: SignalValue = { value: s.value, prev: s.prev, changed: true };
      return { ...w, state, error: null, history };
    });
  }

  async function subscribeAll(): Promise<void> {
    if (!app.connected) return;
    listError = null;
    for (const w of watched) {
      try {
        await app.proto.subscribeSignal(w.name, w.bus);
        await seedValue(w.name, w.bus);
      } catch (e) {
        listError = e instanceof Error ? e.message : String(e);
      }
    }
  }

  function sparkPath(history: number[]): string {
    if (history.length < 2) return '';
    const min = Math.min(...history);
    const max = Math.max(...history);
    const span = max - min || 1;
    const W = 280, H = 40;
    const stepX = W / (SPARK_LEN - 1);
    return history.map((v, i) => {
      const x = (i + (SPARK_LEN - history.length)) * stepX;
      const y = H - ((v - min) / span) * (H - 4) - 2;
      return `${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`;
    }).join(' ');
  }

  onMount(() => {
    app.proto.onSignal(onPush);
    if (app.connected) void subscribeAll();
  });
  onDestroy(() => {
    app.proto.onSignal(null);
    if (app.connected) {
      void app.proto.unsubscribeSignal().catch(() => { /* ignore */ });
    }
  });

  $effect(() => {
    if (app.connected) {
      untrack(() => void subscribeAll());
    } else {
      watched = untrack(() => watched.map(w => ({ ...w, state: null, error: null })));
    }
  });

  // Suggestions from the loaded DBC, if any.
  const suggestions = $derived(dbcStore.signals.slice(0, 200));
  const suggestionList = $derived(suggestions.map(s => s.name));
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 10px">
  <SectionHead title="Dashboard" sub="Live signal watch list — subscribes to the firmware push stream." />

  <div class="frame">
    <div class="frame__body row-flex" style="gap: 8px; flex-wrap: wrap">
      <input
        list="dc-signal-suggestions"
        class="inp"
        placeholder="signal name (e.g. DI_vehicleSpeed)"
        bind:value={signalInput}
        onkeydown={(e) => e.key === 'Enter' && add()}
        style="flex: 1; min-width: 220px"
      />
      <datalist id="dc-signal-suggestions">
        {#each suggestionList as n}<option value={n}></option>{/each}
      </datalist>
      <select bind:value={busInput} class="sel" style="width: auto">
        <option value={0}>bus 0</option>
        <option value={1}>bus 1</option>
      </select>
      <button class="btn btn--sm btn--primary" onclick={add} disabled={!signalInput.trim()}>
        <Icon name="up" size={13} />Add signal
      </button>
    </div>
  </div>

  {#if !app.connected}
    <div class="empty">Not connected — values won't update until you connect.</div>
  {/if}
  {#if listError}
    <div class="empty" style="border-color: var(--dc-err-border); color: var(--dc-err-text)">{listError}</div>
  {/if}

  {#if watched.length === 0}
    <div class="empty">
      No signals watched yet. Type a signal name above and press Add.
      {#if dbcStore.signals.length === 0}
        Load a DBC under <strong>DBC</strong> to get autocomplete.
      {/if}
    </div>
  {/if}

  <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 10px">
    {#each watched as w, i (w.name + ':' + w.bus)}
      <div class="frame">
        <div class="frame__head">
          <span class="mono" style="text-transform: none; letter-spacing: 0; color: var(--dc-text-dim); font-size: 12px; overflow: hidden; text-overflow: ellipsis">
            {w.name}
          </span>
          <span class="row-flex">
            <span class="mono ghost" style="font-size: 10px">bus {w.bus}</span>
            <button class="btn btn--sm btn--ghost btn--icon" onclick={() => remove(i)} aria-label="Remove">
              <Icon name="x" size={12} />
            </button>
          </span>
        </div>
        <div class="frame__body">
          <div class="mono" style="font-size: 24px; font-weight: 600; color: var(--dc-text); display: flex; align-items: baseline; gap: 6px">
            <span>{w.state ? w.state.value.toFixed(2) : '—'}</span>
            {#if w.state && w.state.prev !== w.state.value}
              <span style="font-size: 11px; color: var(--dc-text-fade); font-weight: 400">
                Δ {(w.state.value - w.state.prev).toFixed(2)}
              </span>
            {/if}
          </div>
          {#if w.error}
            <div class="mono" style="color: var(--dc-err-text); font-size: 10.5px; margin-top: 4px">{w.error}</div>
          {/if}
          {#if w.history.length >= 2}
            <svg width="100%" height="40" viewBox="0 0 280 40" preserveAspectRatio="none" style="margin-top: 4px">
              <path d={sparkPath(w.history)} fill="none" stroke="var(--dc-accent)" stroke-width="1.2" />
            </svg>
          {:else}
            <div class="mono ghost" style="font-size: 10px; margin-top: 4px">waiting for samples…</div>
          {/if}
        </div>
      </div>
    {/each}
  </div>
</div>
