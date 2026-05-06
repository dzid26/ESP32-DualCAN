<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import { SAMPLE_EVENTS, type EventTile } from '../../lib/sampleData';
  import Icon from '../Icon.svelte';
  import { onMount } from 'svelte';

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

  onMount(refresh);
  $effect(() => { void app.connected; if (app.connected) refresh(); else actions = []; });
</script>

<div class="view view--events">
  <header class="view__head">
    <div>
      <h2 class="view__title">Events</h2>
      <p class="view__sub">Single-shot Berry actions. Tap to fire. The agent can fire any of these as a tool.</p>
    </div>
    <div class="row-flex">
      <input class="inp" placeholder="Filter…" bind:value={filter} style="width: 180px" />
      <button class="btn btn--sm" onclick={() => app.setView('scripts')} title="Add by writing a Berry script that calls action_register()">
        <Icon name="up" size={13} />New event
      </button>
    </div>
  </header>

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
    <button class="evtile evtile--add" onclick={() => app.setView('scripts')}>
      <div class="evtile__icon">+</div>
      <div class="evtile__name">Add event</div>
      <div class="evtile__desc mono">Write a script that registers an action</div>
    </button>
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
