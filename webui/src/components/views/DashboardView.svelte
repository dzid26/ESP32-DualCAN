<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import { SAMPLE_EVENTS, type EventTile } from '../../lib/sampleData';
  import type { SignalUpdate, SignalValue } from '../../transport/protocol';
  import { dbcStore } from '../../dbcStore.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { onMount, onDestroy, untrack } from 'svelte';

  // The tile metadata (icon, danger, desc, led) lives client-side. The
  // device only knows action names, so we look up extra info by id and
  // fall back to a plain tile for unknown actions.
  const SAMPLE_BY_ID = new Map(SAMPLE_EVENTS.map(e => [e.id, e] as const));

  function tileFor(name: string): EventTile {
    const s = SAMPLE_BY_ID.get(name);
    if (s) return s;
    return { id: name, name, icon: '⚡', desc: 'no metadata · Berry action', danger: false, lastRun: null };
  }

  let actions = $state<string[]>([]);
  let listError = $state<string | null>(null);
  let pending = $state<EventTile | null>(null);
  let busyId = $state<string | null>(null);
  let filter = $state('');

  // When disconnected, preview SAMPLE_EVENTS so the page isn't blank.
  const tiles = $derived<EventTile[]>(
    app.connected ? actions.map(tileFor) : SAMPLE_EVENTS
  );

  const visible = $derived.by(() => {
    const f = filter.trim().toLowerCase();
    if (!f) return tiles;
    return tiles.filter(e => (e.name + ' ' + e.desc).toLowerCase().includes(f));
  });

  async function refresh(): Promise<void> {
    if (!app.connected) return;
    try {
      const r = await app.proto.listActions();
      actions = r.actions ?? [];
      listError = null;
    } catch (e) {
      listError = e instanceof Error ? e.message : String(e);
    }
  }

  function fire(ev: EventTile): void {
    if (ev.danger) { pending = ev; return; }
    runNow(ev);
  }

  async function runNow(ev: EventTile): Promise<void> {
    pending = null;
    busyId = ev.id;
    try {
      await app.proto.invokeAction(ev.id);
      app.pushLog(`▶ fired "${ev.name}"`, 'info', 'events');
    } catch (e) {
      const m = e instanceof Error ? e.message : String(e);
      app.pushLog(`fire ${ev.name} failed: ${m}`, 'error', 'events');
    } finally {
      busyId = null;
    }
  }

  // ---- Signal watch ----
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
  let signalListError = $state<string | null>(null);

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

  async function addSignal(): Promise<void> {
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

  async function removeSignal(idx: number): Promise<void> {
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
    signalListError = null;
    for (const w of watched) {
      try {
        await app.proto.subscribeSignal(w.name, w.bus);
        await seedValue(w.name, w.bus);
      } catch (e) {
        signalListError = e instanceof Error ? e.message : String(e);
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

  const suggestions = $derived(dbcStore.signals.slice(0, 200));
  const suggestionList = $derived(suggestions.map(s => s.name));

  onMount(() => {
    refresh();
    app.proto.onSignal(onPush);
    if (app.connected) void subscribeAll();
  });
  onDestroy(() => {
    app.proto.onSignal(null);
    if (app.connected) void app.proto.unsubscribeSignal().catch(() => { /* ignore */ });
  });

  $effect(() => { void app.connected; if (app.connected) refresh(); else actions = []; });
  $effect(() => { if (app.view === 'events') refresh(); });
  $effect(() => {
    if (app.connected) {
      untrack(() => void subscribeAll());
    } else {
      watched = untrack(() => watched.map(w => ({ ...w, state: null, error: null })));
    }
  });
</script>

<div class="view view--events">
  <header class="view__head">
    <div>
      <h2 class="view__title">Dashboard</h2>
      <p class="view__sub">Single-shot Berry actions. Tap to fire. The agent can fire any of these as a tool.</p>
    </div>
    <div class="row-flex">
      <input class="inp" placeholder="Filter…" bind:value={filter} style="width: 180px" />
      <button class="btn btn--sm" onclick={() => app.setView('scripts')} title="Add by writing a Berry script that calls action_register()">
        <Icon name="up" size={13} />New event
      </button>
    </div>
  </header>

  <div class="events-scroll">
    {#if listError}
      <div class="empty" style="margin: 12px 16px 0; border-color: var(--dc-err-border); color: var(--dc-err-text)">{listError}</div>
    {/if}

    <div class="evgrid">
      {#each visible as ev (ev.id)}
        <button
          class={'evtile' + (ev.danger ? ' evtile--danger' : '') + (busyId === ev.id ? ' evtile--busy' : '')}
          disabled={!app.connected || app.killed || busyId === ev.id}
          onclick={() => fire(ev)}
          title={ev.danger ? 'Vehicle write — confirm before firing' : 'Fire event'}
        >
          <div class="evtile__icon" aria-hidden="true">{ev.icon}</div>
          {#if ev.led}
            <span class={'evtile__led evtile__led--' + ev.led} aria-label={'status: ' + ev.led} title={'Reported status: ' + ev.led}></span>
          {/if}
          <div class="evtile__name">{ev.name}</div>
          <div class="evtile__desc mono">{ev.desc}</div>
          <div class="evtile__foot">
            {#if ev.danger}
              <span class="evtile__pill evtile__pill--danger">vehicle</span>
            {:else}
              <span class="evtile__pill">safe</span>
            {/if}
            <span class="evtile__last mono">{ev.lastRun ? `· ${ev.lastRun}` : '· never'}</span>
          </div>
          {#if busyId === ev.id}
            <div class="evtile__spin" aria-hidden="true"></div>
          {/if}
        </button>
      {/each}
      {#if app.connected && tiles.length === 0}
        <div class="empty" style="grid-column: 1 / -1">
          No actions registered yet. Write a Berry script that calls
          <span class="mono">action_register("name", fn)</span> and enable it.
        </div>
      {/if}
      <button
        class="evtile evtile--add"
        onclick={() => { app.pendingExample = 'tiles_demo.be'; app.setView('scripts'); }}
        title="Open Scripts with tiles_demo.be loaded as a starting point"
      >
        <div class="evtile__icon">+</div>
        <div class="evtile__name">Add event</div>
        <div class="evtile__desc mono">Open tiles_demo.be in editor</div>
      </button>
    </div>

    <!-- Signal watch section -->
    <div style="padding: 0 12px 12px">
      <SectionHead title="Signals" sub="Live signal watch list — subscribes to the firmware push stream." />

      <div class="frame" style="margin-top: 10px">
        <div class="frame__body row-flex" style="gap: 8px; flex-wrap: wrap">
          <input
            list="dc-signal-suggestions"
            class="inp"
            placeholder="signal name (e.g. DI_vehicleSpeed)"
            bind:value={signalInput}
            onkeydown={(e) => e.key === 'Enter' && addSignal()}
            style="flex: 1; min-width: 220px"
          />
          <datalist id="dc-signal-suggestions">
            {#each suggestionList as n}<option value={n}></option>{/each}
          </datalist>
          <select bind:value={busInput} class="sel" style="width: auto">
            <option value={0}>bus 0</option>
            <option value={1}>bus 1</option>
          </select>
          <button class="btn btn--sm btn--primary" onclick={addSignal} disabled={!signalInput.trim()}>
            <Icon name="up" size={13} />Add signal
          </button>
        </div>
      </div>

      {#if !app.connected}
        <div class="empty" style="margin-top: 10px">Not connected — values won't update until you connect.</div>
      {/if}
      {#if signalListError}
        <div class="empty" style="margin-top: 10px; border-color: var(--dc-err-border); color: var(--dc-err-text)">{signalListError}</div>
      {/if}

      {#if watched.length === 0}
        <div class="empty" style="margin-top: 10px">
          No signals watched yet. Type a signal name above and press Add.
          {#if dbcStore.signals.length === 0}
            Load a DBC under <strong>DBC</strong> to get autocomplete.
          {/if}
        </div>
      {/if}

      <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: 10px; margin-top: 10px">
        {#each watched as w, i (w.name + ':' + w.bus)}
          <div class="frame">
            <div class="frame__head">
              <span class="mono" style="text-transform: none; letter-spacing: 0; color: var(--dc-text-dim); font-size: 12px; overflow: hidden; text-overflow: ellipsis">
                {w.name}
              </span>
              <span class="row-flex">
                <span class="mono ghost" style="font-size: 10px">bus {w.bus}</span>
                <button class="btn btn--sm btn--ghost btn--icon" onclick={() => removeSignal(i)} aria-label="Remove">
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
  </div>

  {#if pending}
    <div
      class="cp-backdrop"
      onclick={() => (pending = null)}
      onkeydown={(e) => e.key === 'Escape' && (pending = null)}
      role="presentation"
    >
      <div
        class="frame"
        onclick={(e) => e.stopPropagation()}
        onkeydown={(e) => e.stopPropagation()}
        style="width: min(440px, 92vw); background: var(--dc-surface)"
        role="dialog"
        aria-modal="true"
        tabindex="0"
      >
        <div class="frame__head" style="color: var(--dc-accent-hi)">
          <span class="row-flex">
            <Icon name="bolt" size={13} />
            <span>Confirm vehicle write</span>
          </span>
          <button class="btn btn--sm btn--ghost btn--icon" onclick={() => (pending = null)} aria-label="Cancel">
            <Icon name="x" size={13} />
          </button>
        </div>
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
          <p style="margin: 0; font-size: 14px; color: var(--dc-text)"><strong>{pending.name}</strong></p>
          <div class="mono" style="font-size: 11px; color: var(--dc-text-fade); background: var(--dc-bg); border: 1px solid var(--dc-border); padding: 8px; border-radius: 3px">
            {pending.desc}
          </div>
          <p style="margin: 0; font-size: 12px; color: var(--dc-text-fade); line-height: 1.5">
            This action writes to the vehicle bus. Make sure the car is in a safe state.
            Kill switch is
            {#if app.killed}
              <strong style="color: var(--dc-err-text)">engaged</strong>
            {:else}
              <span>off</span>
            {/if}.
          </p>
          <div class="row-flex" style="justify-content: flex-end">
            <button class="btn btn--sm btn--ghost" onclick={() => (pending = null)}>Cancel</button>
            <button class="btn btn--sm btn--primary" onclick={() => runNow(pending!)}>
              <Icon name="bolt" size={13} />Fire
            </button>
          </div>
        </div>
      </div>
    </div>
  {/if}
</div>
