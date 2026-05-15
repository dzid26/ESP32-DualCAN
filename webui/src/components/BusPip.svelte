<script lang="ts">
  import { app } from '../lib/store.svelte';
  import type { BusStatus } from '../transport/protocol';

  let { id, name, status, rate }:
    { id: number; name: string; status: BusStatus; rate: string } = $props();

  const dbcName = $derived(app.loadedDbc[id]);

  const dotClass = $derived(
    status === 'good' ? 'pip__dot--ok' :
    status !== 'idle' ? 'pip__dot--err' : ''
  );

  const label = $derived(
    status === 'good'     ? rate      :
    status === 'tx_error' ? 'tx err'  :
    status === 'rx_error' ? 'rx err'  :
    status === 'error'    ? 'bus off'  : 'idle'
  );

  function gotoDbc(e: MouseEvent): void {
    e.stopPropagation();
    app.dbcViewBus = id;
    app.setView('dbc');
  }
</script>

<div class="pip" title={`${name} · bus ${id}${dbcName ? ' · ' + dbcName : ''}`}>
  <span class="mono" style="color: var(--dc-text-dim)">bus{id}</span>
  <span class={'pip__dot ' + dotClass}></span>
  <span class="ghost mono" style="font-size: 10px; min-width: 4.5ch; display: inline-block">{label}</span>
  {#if dbcName}
    <button
      class="dbclink mono"
      onclick={gotoDbc}
      title={`Open DBC view for bus ${id} (${dbcName})`}
    >(dbc)</button>
  {/if}
</div>

<style>
  .dbclink {
    background: transparent;
    border: none;
    padding: 0 2px;
    color: var(--dc-accent);
    font-size: 10px;
    cursor: pointer;
    text-decoration: underline dotted;
    text-underline-offset: 2px;
  }
  .dbclink:hover { color: var(--dc-accent-hi); }
</style>
