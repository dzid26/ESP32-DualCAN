<script lang="ts">
  import type { Transport } from '../transport/types';
  import type { Protocol } from '../transport/protocol';
  import { onMount } from 'svelte';

  let { transport: _transport, proto, connected }: { transport: Transport; proto: Protocol; connected: boolean } = $props();

  let actions = $state<string[]>([]);
  let busy = $state<string | null>(null);
  let error = $state<string | null>(null);

  async function refresh() {
    if (!connected) return;
    try {
      const r = await proto.listActions();
      actions = r.actions ?? [];
      error = null;
    } catch (e: any) {
      error = e.message ?? String(e);
    }
  }

  async function invoke(name: string) {
    busy = name;
    error = null;
    try {
      await proto.invokeAction(name);
    } catch (e: any) {
      error = e.message ?? String(e);
    } finally {
      busy = null;
    }
  }

  onMount(refresh);
  $effect(() => { if (connected) refresh(); });
</script>

<div class="dashboard">
  <h2>Dashboard</h2>

  {#if !connected}
    <div class="placeholder">
      Connect to the device via BLE to see live data.
    </div>
  {:else}
    <section>
      <header>
        <h3>Actions</h3>
        <button onclick={refresh}>refresh</button>
      </header>
      {#if error}
        <div class="err">{error}</div>
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
  header {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-top: 1rem;
  }
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
  .tile:hover:not(:disabled) {
    border-color: #555;
    background: #222238;
  }
  .err {
    color: #f55;
    margin-top: 0.5rem;
  }
</style>
