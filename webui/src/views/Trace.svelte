<script lang="ts">
  import type { Protocol, TraceFrame } from '../transport/protocol';
  import { onDestroy } from 'svelte';

  let { proto, connected }: { proto: Protocol; connected: boolean } = $props();

  // Most-recent-first ring buffer for display.
  const MAX_ROWS = 1000;
  let frames = $state<TraceFrame[]>([]);
  let buffer: TraceFrame[] = [];   // accumulated since last flush

  let running = $state(false);
  let paused = $state(false);
  let bus0 = $state(true);
  let bus1 = $state(true);
  let filterId = $state('');       // hex pattern, optional
  let busFilter = $state(-1);      // -1 = both
  let status = $state('');
  let totalCount = $state(0);
  let droppedCount = $state(0);

  function hex(b: Uint8Array): string {
    return Array.from(b, x => x.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  }

  function idHex(id: number): string {
    return id.toString(16).toUpperCase().padStart(3, '0');
  }

  function onFrame(f: TraceFrame) {
    totalCount++;
    if (paused) return;
    buffer.push(f);
  }

  // Flush at ~20 Hz; avoids one-rerender-per-frame at high rates.
  let flushTimer: ReturnType<typeof setInterval> | null = null;
  function startFlush() {
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
  function stopFlush() {
    if (flushTimer) { clearInterval(flushTimer); flushTimer = null; }
  }

  async function start() {
    if (!connected) return;
    const buses: number[] = [];
    if (bus0) buses.push(0);
    if (bus1) buses.push(1);
    if (buses.length === 0) { status = 'select at least one bus'; return; }
    try {
      proto.onTrace(onFrame);
      await proto.traceStart(buses);
      running = true;
      paused = false;
      startFlush();
      status = `tracing buses ${buses.join(',')}`;
    } catch (e: any) {
      status = `start failed: ${e.message}`;
    }
  }

  async function stop() {
    try {
      await proto.traceStop();
    } catch (e: any) {
      status = `stop failed: ${e.message}`;
    } finally {
      proto.onTrace(null);
      running = false;
      stopFlush();
      status = `stopped (rx=${totalCount}, dropped=${droppedCount})`;
    }
  }

  function clear() {
    frames = [];
    buffer = [];
    totalCount = 0;
    droppedCount = 0;
  }

  /* Parse a previously exported trace file (the candump-ish format above)
   * back into TraceFrame[]. Unparseable lines are silently skipped. */
  const PARSE_RE = /^\((\d+(?:\.\d+)?)\)\s+bus(\d+)\s+([0-9A-Fa-f]+)\s+\[(\d+)\]\s*([0-9A-Fa-f\s]*)$/;

  function parseCapture(text: string): TraceFrame[] {
    const out: TraceFrame[] = [];
    for (const raw of text.split(/\r?\n/)) {
      const line = raw.trim();
      if (!line || line.startsWith('#')) continue;
      const m = line.match(PARSE_RE);
      if (!m) continue;
      const tsMs = Math.round(parseFloat(m[1]) * 1000);
      const bus  = parseInt(m[2], 10);
      const id   = parseInt(m[3], 16);
      const dlc  = parseInt(m[4], 10);
      const bytes = m[5].trim().split(/\s+/).filter(Boolean).map(b => parseInt(b, 16));
      if (bytes.length !== dlc) continue;   // malformed
      out.push({ ts: tsMs, bus, id, data: new Uint8Array(bytes) });
    }
    return out;
  }

  function handleLoadFile(e: Event) {
    const input = e.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';   // allow loading the same file twice
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const parsed = parseCapture(reader.result as string);
      if (parsed.length === 0) { status = `${file.name}: no frames parsed`; return; }
      // Newest-first to match the live view ordering. parseCapture yields
      // file order (oldest-first in our exporter); reverse to flip.
      frames = parsed.slice().reverse();
      buffer = [];
      totalCount = parsed.length;
      droppedCount = 0;
      status = `loaded ${parsed.length} frames from ${file.name}`;
    };
    reader.readAsText(file);
  }

  function exportCandump() {
    // candump-ish: (ts.sss) busN  XXX   [DLC]  AA BB CC ...
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

  function matchesIdFilter(idStr: string, pattern: string) {
    if (!pattern) return true;
    return idStr.includes(pattern.toUpperCase());
  }

  let visible = $derived(frames.filter(f =>
    (busFilter < 0 || f.bus === busFilter) &&
    matchesIdFilter(idHex(f.id), filterId.trim())
  ));

  onDestroy(() => {
    if (running) {
      proto.traceStop().catch(() => {});
      proto.onTrace(null);
    }
    stopFlush();
  });

  $effect(() => {
    // Stop trace if connection drops.
    if (!connected && running) {
      running = false;
      stopFlush();
      proto.onTrace(null);
      status = 'disconnected';
    }
  });
</script>

<div class="trace">
  <h2>Trace</h2>
  <p>Live CAN frame log streamed from the device. Use to reverse-engineer unknown messages.</p>

  <div class="controls">
    <label class="bus-cb"><input type="checkbox" bind:checked={bus0} disabled={running} /> bus 0</label>
    <label class="bus-cb"><input type="checkbox" bind:checked={bus1} disabled={running} /> bus 1</label>

    {#if !running}
      <button class="primary" onclick={start} disabled={!connected}>Start</button>
    {:else}
      <button onclick={() => paused = !paused}>{paused ? 'Resume' : 'Pause'}</button>
      <button class="danger" onclick={stop}>Stop</button>
    {/if}

    <button onclick={clear} disabled={frames.length === 0}>Clear</button>
    <button onclick={exportCandump} disabled={frames.length === 0}>Export</button>
    <label class="load-btn" title="Load a previously exported trace file">
      Load
      <input type="file" accept=".log,text/plain" onchange={handleLoadFile} />
    </label>
  </div>

  <div class="filter-row">
    <input class="filter" bind:value={filterId} placeholder="filter by id (hex substring, e.g. 1A4)" />
    <select bind:value={busFilter} title="display filter">
      <option value={-1}>all buses</option>
      <option value={0}>bus 0 only</option>
      <option value={1}>bus 1 only</option>
    </select>
    <span class="counts">rx: {totalCount} · shown: {visible.length}{droppedCount > 0 ? ` · dropped: ${droppedCount}` : ''}</span>
  </div>

  <p class="status">{status}</p>

  {#if visible.length > 0}
    <table class="frames">
      <thead>
        <tr><th>ts (s)</th><th>bus</th><th>id</th><th>dlc</th><th>data</th></tr>
      </thead>
      <tbody>
        {#each visible as f (f.ts + ':' + f.bus + ':' + f.id)}
          <tr>
            <td class="dim">{(f.ts / 1000).toFixed(3)}</td>
            <td class="dim">{f.bus}</td>
            <td class="id">{idHex(f.id)}</td>
            <td class="dim">{f.data.length}</td>
            <td class="data">{hex(f.data)}</td>
          </tr>
        {/each}
      </tbody>
    </table>
  {:else}
    <div class="placeholder">
      {#if !connected}
        Connect to start tracing.
      {:else if !running}
        Press Start to begin streaming frames.
      {:else}
        Waiting for frames…
      {/if}
    </div>
  {/if}
</div>

<style>
  .controls, .filter-row {
    display: flex;
    gap: 0.5rem;
    align-items: center;
    margin: 0.5rem 0;
    flex-wrap: wrap;
  }
  .bus-cb {
    color: #aaa;
    font-size: 0.85rem;
    display: inline-flex;
    align-items: center;
    gap: 0.25rem;
  }
  .filter {
    flex: 1;
    background: #0d0d1a;
    border: 1px solid #333;
    border-radius: 4px;
    color: #ddd;
    padding: 0.35rem 0.6rem;
    font-family: monospace;
    font-size: 0.85rem;
  }
  select {
    background: #0d0d1a;
    border: 1px solid #333;
    border-radius: 4px;
    color: #ddd;
    padding: 0.35rem 0.5rem;
    font-size: 0.85rem;
  }
  .counts {
    color: #777;
    font-size: 0.8rem;
    font-family: monospace;
  }
  button {
    background: #2a2a4a;
    border: 1px solid #444;
    color: #ccc;
    padding: 0.4rem 0.9rem;
    border-radius: 4px;
    cursor: pointer;
  }
  button.primary { background: #2a4a2a; border-color: #4a4; }
  button.danger  { background: #4a1a1a; border-color: #a44; }
  button:disabled { opacity: 0.4; cursor: not-allowed; }
  .load-btn {
    display: inline-flex;
    align-items: center;
    background: #2a2a4a;
    border: 1px solid #444;
    color: #ccc;
    padding: 0.4rem 0.9rem;
    border-radius: 4px;
    cursor: pointer;
    font-size: 0.85rem;
  }
  .load-btn input[type="file"] { display: none; }
  .placeholder {
    background: #0d0d1a;
    border: 1px dashed #333;
    border-radius: 4px;
    padding: 1.5rem;
    text-align: center;
    color: #666;
  }
  .status { color: #888; font-size: 0.85rem; min-height: 1.2em; }

  table.frames {
    width: 100%;
    border-collapse: collapse;
    font-family: monospace;
    font-size: 0.8rem;
    table-layout: fixed;
  }
  table.frames th, table.frames td {
    padding: 0.2rem 0.4rem;
    text-align: left;
    border-bottom: 1px solid #1a1a2a;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  table.frames th { color: #777; font-weight: normal; }
  table.frames td.id { color: #aad; }
  table.frames td.data { color: #aea; }
  table.frames td.dim { color: #666; }
</style>
