<script lang="ts">
  import { BleTransport } from './transport/ble';
  import ScriptsView from './views/Scripts.svelte';
  import DbcView from './views/Dbc.svelte';
  import DashboardView from './views/Dashboard.svelte';

  const ble = new BleTransport();
  let activeView = $state('scripts');
  let connected = $state(false);

  let error = $state('');

  async function toggleConnect() {
    error = '';
    try {
      if (connected) {
        await ble.disconnect();
      } else {
        await ble.connect();
      }
      connected = ble.connected;
    } catch (e: any) {
      error = e?.message || String(e);
      console.error('BLE error:', e);
      connected = false;
    }
  }
</script>

<main>
  <nav>
    <span class="brand">Dorky Commander</span>
    <button class:active={activeView === 'scripts'} onclick={() => activeView = 'scripts'}>Scripts</button>
    <button class:active={activeView === 'dbc'} onclick={() => activeView = 'dbc'}>DBC</button>
    <button class:active={activeView === 'dashboard'} onclick={() => activeView = 'dashboard'}>Dashboard</button>
    <button class="connect" class:connected onclick={toggleConnect}>
      {connected ? 'Connected' : 'Connect BLE'}
    </button>
  </nav>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  <div class="content">
    {#if activeView === 'scripts'}
      <ScriptsView transport={ble} />
    {:else if activeView === 'dbc'}
      <DbcView transport={ble} />
    {:else}
      <DashboardView transport={ble} />
    {/if}
  </div>
</main>

<style>
  :global(body) {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: #1a1a2e;
    color: #e0e0e0;
  }
  main {
    max-width: 960px;
    margin: 0 auto;
    padding: 1rem;
  }
  nav {
    display: flex;
    gap: 0.5rem;
    align-items: center;
    padding: 0.5rem 0;
    border-bottom: 1px solid #333;
    margin-bottom: 1rem;
  }
  .brand {
    font-weight: bold;
    font-size: 1.1rem;
    margin-right: auto;
  }
  nav button {
    background: #2a2a4a;
    border: 1px solid #444;
    color: #ccc;
    padding: 0.4rem 0.8rem;
    border-radius: 4px;
    cursor: pointer;
  }
  nav button.active {
    background: #3a3a6a;
    border-color: #6a6aaa;
    color: #fff;
  }
  nav button.connect {
    margin-left: 1rem;
  }
  nav button.connected {
    border-color: #4a4;
    color: #4a4;
  }
  .error {
    background: #4a1a1a;
    border: 1px solid #a44;
    color: #faa;
    padding: 0.5rem 0.75rem;
    border-radius: 4px;
    font-size: 0.85rem;
    margin-bottom: 1rem;
  }
</style>
