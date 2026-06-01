<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import type { TraceFrame } from '../../transport/protocol';
  import { dbcStore } from '../../dbcStore.svelte';
  import { evalCond, computeDiff, CONDS } from '../../lib/capture';
  import type { Cond, Snapshot } from '../../lib/capture';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { onDestroy } from 'svelte';

  // ---- shared helpers ----
  function hex(b: Uint8Array): string {
    return Array.from(b, x => x.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  }
  function idHex(id: number): string {
    return id.toString(16).toUpperCase().padStart(3, '0');
  }

  // ---- Baseline diff ----
  let snapA = $state<Snapshot | null>(null);
  let snapB = $state<Snapshot | null>(null);
  let snapBusy = $state(false);
  let snapError = $state<string | null>(null);

  async function snap(slot: 'a' | 'b'): Promise<void> {
    if (!app.connected || !dbcStore.signals.length) return;
    snapBusy = true;
    snapError = null;
    const bus = 0;
    try {
      const results = await Promise.all(
        dbcStore.signals.map(async (s) => {
          try {
            const v = await app.proto.getSignalValue(s.name, bus);
            return [s.name, v?.value ?? null] as [string, number | null];
          } catch {
            return [s.name, null] as [string, number | null];
          }
        })
      );
      const snapshot: Snapshot = { ts: new Date().toLocaleTimeString(), values: new Map(results) };
      if (slot === 'a') { snapA = snapshot; snapB = null; }
      else snapB = snapshot;
    } catch (e) {
      snapError = e instanceof Error ? e.message : String(e);
    } finally {
      snapBusy = false;
    }
  }

  const diffRows = $derived(snapA && snapB ? computeDiff(snapA, snapB) : []);

  // ---- Event-triggered capture ----

  let trigSignal = $state('');
  let trigCond = $state<Cond>('>');
  let trigValue = $state(0);
  let trigDuration = $state(3000);
  let armed = $state(false);
  let capturing = $state(false);
  let captureStatus = $state('');
  let capturedFrames = $state<TraceFrame[]>([]);
  let capturedName = $state('');

  let pollTimer: ReturnType<typeof setInterval> | null = null;
  let captureTimer: ReturnType<typeof setTimeout> | null = null;
  let captureBuffer: TraceFrame[] = [];
  let pollInflight = false;

  async function arm(): Promise<void> {
    if (!app.connected || !trigSignal) return;
    armed = true;
    capturing = false;
    captureStatus = `armed — waiting for ${trigSignal} ${trigCond} ${trigValue}`;
    captureBuffer = [];
    const bus = 0;

    pollTimer = setInterval(async () => {
      if (!app.connected) { disarm(); return; }
      if (capturing || pollInflight) return;
      pollInflight = true;
      try {
        const v = await app.proto.getSignalValue(trigSignal, bus);
        if (v === null) {
          captureStatus = `signal "${trigSignal}" not found — check DBC / bus`;
          pollInflight = false;
          return;
        }
        if (evalCond(v.value, trigCond, trigValue)) {
          capturing = true;
          captureStatus = `triggered (${trigSignal}=${v.value.toFixed(3)}) — capturing ${trigDuration} ms…`;
          clearInterval(pollTimer!); pollTimer = null;

          app.proto.onTrace((f) => { captureBuffer.push(f); });
          try { await app.proto.traceStart([bus]); } catch { /* best-effort */ }

          captureTimer = setTimeout(async () => {
            try { await app.proto.traceStop(); } catch { /* best-effort */ }
            app.proto.onTrace(null);
            capturedFrames = [...captureBuffer];
            capturedName = `cap-${new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-')}`;
            capturing = false;
            armed = false;
            captureStatus = `done — ${capturedFrames.length} frames saved`;
          }, trigDuration);
        }
      } catch { /* disconnect mid-poll — handled above */ }
      pollInflight = false;
    }, 500);
  }

  function disarm(): void {
    if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
    if (captureTimer) { clearTimeout(captureTimer); captureTimer = null; }
    if (capturing) {
      app.proto.traceStop().catch(() => {});
      app.proto.onTrace(null);
    }
    armed = false;
    capturing = false;
    captureStatus = captureStatus ? 'disarmed' : '';
  }

  onDestroy(disarm);

  $effect(() => {
    if (!app.connected && (armed || capturing)) disarm();
  });

  function downloadBlob(blob: Blob, name: string): void {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = name; a.click();
    URL.revokeObjectURL(url);
  }

  function exportCandump(): void {
    if (!capturedFrames.length) return;
    const lines = capturedFrames.map(f =>
      `(${(f.ts / 1000).toFixed(3)}) bus${f.bus}  ${idHex(f.id)}   [${f.data.length}]  ${hex(f.data)}`);
    downloadBlob(new Blob([lines.join('\n') + '\n'], { type: 'text/plain' }), capturedName + '.log');
  }
  function exportCsv(): void {
    if (!capturedFrames.length) return;
    const head = 'ts_ms,bus,id_hex,dlc,data_hex\n';
    const lines = capturedFrames.map(f =>
      `${f.ts},${f.bus},0x${idHex(f.id)},${f.data.length},${hex(f.data).replace(/\s+/g,'')}`);
    downloadBlob(new Blob([head + lines.join('\n') + '\n'], { type: 'text/csv' }), capturedName + '.csv');
  }
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1">
  <SectionHead title="Capture & diff" sub="Event-triggered captures, baseline diffs" />

  {#if !app.connected}
    <div class="empty" style="margin-bottom: 10px">Connect to device to use captures.</div>
  {/if}

  <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 10px">

    <!-- Event capture -->
    <div class="frame">
      <div class="frame__head">Event capture</div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
        <label class="field">
          <span>Signal</span>
          <input class="inp" bind:value={trigSignal} list="cap-signals"
            placeholder="e.g. DriverDoorHandle" disabled={armed} />
          <datalist id="cap-signals">
            {#each dbcStore.signals as s}
              <option value={s.name}>{s.message} · {s.name}</option>
            {/each}
          </datalist>
        </label>
        <div class="field">
          <span>Condition</span>
          <div style="display: flex; gap: 4px">
            <select class="sel" bind:value={trigCond} disabled={armed} style="width: 60px">
              {#each CONDS as c}<option value={c}>{c}</option>{/each}
            </select>
            <input class="inp" type="number" bind:value={trigValue} disabled={armed} style="width: 80px" />
          </div>
        </div>
        <label class="field">
          <span>Duration</span>
          <div style="display: flex; align-items: center; gap: 4px">
            <input class="inp" type="number" bind:value={trigDuration}
              min={100} max={30000} step={100} disabled={armed} style="width: 80px" />
            <span class="ghost" style="font-size: 11px">ms</span>
          </div>
        </label>
        <div style="display: flex; gap: 6px; align-items: center; flex-wrap: wrap">
          {#if armed}
            <button class="btn btn--sm btn--danger" onclick={disarm}>
              <Icon name="stop" size={13} />Disarm
            </button>
          {:else}
            <button class="btn btn--primary btn--sm" onclick={arm}
              disabled={!app.connected || !trigSignal.trim()}>
              <Icon name="play" size={13} />Arm
            </button>
          {/if}
          {#if capturedFrames.length}
            <button class="btn btn--sm" onclick={exportCsv} title="Export as CSV">CSV</button>
            <button class="btn btn--sm" onclick={exportCandump} title="Export as candump log">candump</button>
          {/if}
        </div>
        {#if captureStatus}
          <span class="mono ghost" style="font-size: 11px">{captureStatus}</span>
        {/if}
      </div>
    </div>

    <!-- Baseline diff -->
    <div class="frame">
      <div class="frame__head">Baseline diff</div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
        {#if !dbcStore.signals.length}
          <span class="ghost" style="font-size: 11px">Load and upload a DBC first to enumerate signals.</span>
        {:else}
          <div class="row-flex">
            <span class="snap-label" style="color: var(--dc-info-text)">A</span>
            <span class="ghost" style="flex: 1; font-size: 11px">
              {snapA ? `${snapA.values.size} signals @ ${snapA.ts}` : '—'}
            </span>
            <button class="btn btn--sm" onclick={() => snap('a')}
              disabled={!app.connected || snapBusy}>Snap</button>
          </div>
          <div class="row-flex">
            <span class="snap-label" style="color: var(--dc-accent)">B</span>
            <span class="ghost" style="flex: 1; font-size: 11px">
              {snapB ? `${snapB.values.size} signals @ ${snapB.ts}` : '—'}
            </span>
            <button class="btn btn--sm" onclick={() => snap('b')}
              disabled={!app.connected || snapBusy || !snapA}>Snap</button>
          </div>
          {#if snapBusy}
            <span class="ghost mono" style="font-size: 11px">polling {dbcStore.signals.length} signals…</span>
          {/if}
          {#if snapError}
            <span class="mono" style="color: var(--dc-err-text); font-size: 11px">{snapError}</span>
          {/if}
        {/if}
      </div>
    </div>

    <!-- Replay stub (needs firmware can.send op) -->
    <div class="frame" style="opacity: 0.45">
      <div class="frame__head">Replay</div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 6px">
        <span class="muted mono" style="font-size: 11px">
          {capturedFrames.length
            ? `${capturedName} · ${capturedFrames.length} frames`
            : 'no captures — arm event capture above'}
        </span>
        <div class="row-flex" style="margin-top: 4px">
          <button class="btn btn--sm" disabled><Icon name="play" size={13} />Sim</button>
          <button class="btn btn--sm btn--danger" disabled><Icon name="play" size={13} />Live</button>
        </div>
        <span class="ghost" style="font-size: 10px">needs firmware can.send — coming later</span>
      </div>
    </div>

  </div>

  <!-- Diff results table -->
  {#if snapA && snapB}
    <div class="frame" style="margin-top: 10px">
      <div class="frame__head">
        Diff A→B
        <span class="ghost" style="font-size: 10px; margin-left: 8px">{snapA.ts} → {snapB.ts}</span>
        {#if diffRows.length > 0}
          <span class="pill pill--info" style="margin-left: 6px; font-size: 10px">{diffRows.length} changed</span>
        {/if}
      </div>
      {#if diffRows.length === 0}
        <div class="frame__body"><span class="ghost" style="font-size: 12px">No signals changed</span></div>
      {:else}
        <div class="frame__body" style="padding: 0">
          <div class="diff-head">
            <span>Signal</span><span>A</span><span>B</span><span>Δ</span>
          </div>
          {#each diffRows as row}
            <div class="diff-row">
              <span class="mono" style="font-size: 11px; overflow: hidden; text-overflow: ellipsis">{row.name}</span>
              <span class="mono ghost" style="font-size: 11px">{row.a?.toFixed(3)}</span>
              <span class="mono" style="font-size: 11px; color: var(--dc-accent)">{row.b?.toFixed(3)}</span>
              <span class="mono" style="font-size: 11px; color: {row.delta > 0 ? 'var(--dc-ok)' : 'var(--dc-err-text)'}">
                {row.delta > 0 ? '+' : ''}{row.delta.toFixed(3)}
              </span>
            </div>
          {/each}
        </div>
      {/if}
    </div>
  {/if}
</div>

<style>
  .field {
    display: grid; grid-template-columns: 90px 1fr; gap: 8px;
    align-items: center; font-size: 12px; color: var(--dc-text-dim);
  }
  .snap-label {
    font-family: var(--dc-mono); font-weight: 700; font-size: 13px; min-width: 14px;
  }
  .diff-head {
    display: grid; grid-template-columns: 1fr 90px 90px 80px;
    padding: 4px 12px; font-size: 10px; color: var(--dc-text-fade);
    border-bottom: 1px solid var(--dc-border); text-transform: uppercase; letter-spacing: 0.05em;
  }
  .diff-row {
    display: grid; grid-template-columns: 1fr 90px 90px 80px;
    padding: 5px 12px; border-bottom: 1px solid var(--dc-border-faint, #2a2218);
    align-items: center;
  }
  .diff-row:last-child { border-bottom: none; }
</style>
