<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import type { TraceFrame } from '../../transport/protocol';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { onDestroy } from 'svelte';

  const MAX_ROWS = 1000;
  let frames = $state<TraceFrame[]>([]);
  let buffer: TraceFrame[] = [];

  let running = $state(false);
  let paused = $state(false);
  let bus0 = $state(true);
  let bus1 = $state(true);
  let filterId = $state('');
  let busFilter = $state(-1);
  let status = $state<string>('');
  let totalCount = $state(0);
  let droppedCount = $state(0);

  function hex(b: Uint8Array): string {
    return Array.from(b, x => x.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  }
  function idHex(id: number): string {
    return id.toString(16).toUpperCase().padStart(3, '0');
  }

  function onFrame(f: TraceFrame): void {
    totalCount++;
    if (paused) return;
    buffer.push(f);
  }

  let flushTimer: ReturnType<typeof setInterval> | null = null;
  function startFlush(): void {
    if (flushTimer) return;
    flushTimer = setInterval(() => {
      if (buffer.length === 0) return;
      const merged = buffer.concat(frames);
      buffer = [];
      if (merged.length > MAX_ROWS) {
        droppedCount += merged.length - MAX_ROWS;
        frames = merged.slice(0, MAX_ROWS);
      } else {
        frames = merged;
      }
    }, 50);
  }
  function stopFlush(): void {
    if (flushTimer) { clearInterval(flushTimer); flushTimer = null; }
  }

  async function start(): Promise<void> {
    if (!app.connected) return;
    const buses: number[] = [];
    if (bus0) buses.push(0);
    if (bus1) buses.push(1);
    if (buses.length === 0) { status = 'select at least one bus'; return; }
    try {
      app.proto.onTrace(onFrame);
      await app.proto.traceStart(buses);
      running = true;
      paused = false;
      startFlush();
      status = `tracing buses ${buses.join(',')}`;
    } catch (e) {
      status = `start failed: ${e instanceof Error ? e.message : e}`;
    }
  }

  async function stop(): Promise<void> {
    try {
      await app.proto.traceStop();
    } catch (e) {
      status = `stop failed: ${e instanceof Error ? e.message : e}`;
    } finally {
      app.proto.onTrace(null);
      running = false;
      stopFlush();
      status = `stopped (rx=${totalCount}, dropped=${droppedCount})`;
    }
  }

  function clear(): void {
    frames = [];
    buffer = [];
    totalCount = 0;
    droppedCount = 0;
  }

  function exportCandump(): void {
    const lines = [...frames].reverse().map(f => {
      const sec = (f.ts / 1000).toFixed(3);
      return `(${sec}) bus${f.bus}  ${idHex(f.id)}   [${f.data.length}]  ${hex(f.data)}`;
    });
    const blob = new Blob([lines.join('\n') + '\n'], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `trace-${new Date().toISOString().replace(/[:.]/g, '-')}.log`;
    a.click();
    URL.revokeObjectURL(url);
  }

  function exportCsv(): void {
    const head = 'ts_ms,bus,id_hex,dlc,data_hex\n';
    const lines = [...frames].reverse().map(f =>
      `${f.ts},${f.bus},0x${idHex(f.id)},${f.data.length},${hex(f.data).replace(/\s+/g,'')}`);
    const blob = new Blob([head + lines.join('\n') + '\n'], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `trace-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  }

  function matchesIdFilter(id: number, pattern: string): boolean {
    if (!pattern) return true;
    return idHex(id).includes(pattern.toUpperCase().replace(/^0X/, ''));
  }

  const visible = $derived(frames.filter(f =>
    (busFilter < 0 || f.bus === busFilter) &&
    matchesIdFilter(f.id, filterId.trim())
  ));

  onDestroy(() => {
    if (running) {
      void app.proto.traceStop().catch(() => { /* ignore */ });
      app.proto.onTrace(null);
    }
    stopFlush();
  });

  $effect(() => {
    if (!app.connected && running) {
      running = false;
      stopFlush();
      app.proto.onTrace(null);
      status = 'disconnected';
    }
  });

  $effect(() => {
    if (app.tracePendingBus !== null) {
      busFilter = app.tracePendingBus;
      app.tracePendingBus = null;
    }
  });
</script>

<div style="padding: 12px; display: flex; flex-direction: column; flex: 1; min-height: 0; gap: 10px">
  <SectionHead title="Trace" sub="Live CAN frames — filter, pause, export" />

  <div class="row-flex" style="flex-wrap: wrap">
    <label class="row-flex" style="gap: 4px; color: var(--dc-text-fade); font-size: 11px">
      <input type="checkbox" bind:checked={bus0} disabled={running} /> bus 0
    </label>
    <label class="row-flex" style="gap: 4px; color: var(--dc-text-fade); font-size: 11px">
      <input type="checkbox" bind:checked={bus1} disabled={running} /> bus 1
    </label>

    {#if !running}
      <button class="btn btn--sm btn--primary" onclick={start} disabled={!app.connected}>
        <Icon name="play" size={13} />Start
      </button>
    {:else}
      <button class="btn btn--sm" onclick={() => (paused = !paused)}>
        {#if paused}<Icon name="play" size={13} />Resume{:else}<Icon name="pause" size={13} />Pause{/if}
      </button>
      <button class="btn btn--sm btn--danger" onclick={stop}><Icon name="stop" size={13} />Stop</button>
    {/if}
    <button class="btn btn--sm" onclick={clear} disabled={frames.length === 0}>Clear</button>

    <div
      class="row-flex"
      style="flex: 1; min-width: 200px; background: var(--dc-bg); border: 1px solid var(--dc-border-2); border-radius: 4px; padding: 0 10px; height: var(--dc-btn-h)"
    >
      <Icon name="filter" size={13} />
      <input
        bind:value={filterId}
        placeholder="filter id (hex substring, e.g. 1A4)"
        class="mono"
        style="background: transparent; border: none; outline: none; color: var(--dc-text); flex: 1; font-size: 12px"
      />
      <select bind:value={busFilter} class="sel" style="width: auto; height: 22px; font-size: 11px">
        <option value={-1}>all buses</option>
        <option value={0}>bus 0</option>
        <option value={1}>bus 1</option>
      </select>
    </div>

    <button
      class="btn btn--sm"
      onclick={exportCsv}
      disabled={frames.length === 0}
      title="Export visible frames as CSV — comma-separated columns ts_ms, bus, id_hex, dlc, data_hex. Spreadsheet-friendly."
    >
      <Icon name="down" size={13} />CSV
    </button>
    <button
      class="btn btn--sm"
      onclick={exportCandump}
      disabled={frames.length === 0}
      title="Export visible frames in Linux candump log format: (sec) busN id [dlc] BB BB ... — replayable with can-utils canplayer and Trace itself."
    >
      candump
    </button>
  </div>

  <div class="row-flex mono" style="font-size: 11px; color: var(--dc-text-fade)">
    <span>{status}</span>
    <span class="spacer"></span>
    <span>rx: {totalCount} · shown: {visible.length}{droppedCount > 0 ? ` · dropped: ${droppedCount}` : ''}</span>
  </div>

  <div class="dtable" style="flex: 1">
    <div class="dtable__head" style="grid-template-columns: 80px 40px 80px 1fr 260px">
      <span>t (ms)</span><span>bus</span><span>id</span><span>name</span><span>data</span>
    </div>
    <div class="dtable__body">
      {#if visible.length === 0}
        <div class="empty" style="margin: 12px">
          {#if !app.connected}Connect to start tracing.
          {:else if !running}Press Start to begin streaming frames.
          {:else}Waiting for frames…{/if}
        </div>
      {/if}
      {#each visible as f, i (i)}
        <div class="dtable__row mono" style="grid-template-columns: 80px 40px 80px 1fr 260px">
          <span>{f.ts}</span>
          <span>{f.bus}</span>
          <span>0x{idHex(f.id)}</span>
          <span style="font-family: var(--dc-font-sans); color: var(--dc-text-fade)">—</span>
          <span style="color: var(--dc-text-dim)">{hex(f.data)}</span>
        </div>
      {/each}
    </div>
  </div>
</div>
