<script lang="ts">
  import { app } from '../lib/store.svelte';

  let { id, name, active, rate }:
    { id: number; name: string; active: boolean; rate: string } = $props();

  const dbcName = $derived(app.loadedDbc[id]);

  function gotoDbc(e: MouseEvent): void {
    e.stopPropagation();
    app.dbcViewBus = id;
    app.setView('dbc');
  }
</script>

<div class="pip" title={`${name} · bus ${id}${dbcName ? ' · ' + dbcName : ''}`}>
  <span class="mono" style="color: var(--dc-text-dim)">bus{id}</span>
  <span class={'pip__dot ' + (active ? 'pip__dot--ok' : '')}></span>
  <span class="ghost mono" style="font-size: 10px">{active ? rate : 'idle'}</span>
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
