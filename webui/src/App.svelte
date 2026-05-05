<script lang="ts">
  import { BleTransport } from './transport/ble';
  import { Protocol } from './transport/protocol';
  import ScriptsView from './views/Scripts.svelte';
  import DbcView from './views/Dbc.svelte';
  import DashboardView from './views/Dashboard.svelte';
  import TraceView from './views/Trace.svelte';
  import SettingsView from './views/Settings.svelte';

  const ble = new BleTransport();
  const proto = new Protocol(ble);
  let activeView = $state('scripts');
  let connected = $state(false);
  let error = $state('');
  let fwVersion = $state<string | null>(null);

  /* Bump in lockstep with the firmware-side proto_version when ops change. */
  const UI_PROTO_VERSION = 1;
  let protoMismatch = $state<string | null>(null);

  ble.onConnectionChange(async (c) => {
    connected = c;
    if (!c) { fwVersion = null; protoMismatch = null; return; }
    try {
      const info = await proto.systemInfo();
      fwVersion = info.fw_version;
      if (info.proto_version !== UI_PROTO_VERSION) {
        protoMismatch =
          `firmware proto v${info.proto_version}, UI expects v${UI_PROTO_VERSION} — some features may not work`;
      } else {
        protoMismatch = null;
      }
    } catch (e: any) {
      // Older firmware doesn't support system.info — that's itself a mismatch.
      protoMismatch = 'firmware too old (no system.info) — please update';
    }
  });

  async function toggleConnect() {
    error = '';
    try {
      if (connected) {
        await ble.disconnect();
      } else {
        await ble.connect();
      }
    } catch (e: any) {
      error = e?.message || String(e);
      console.error('BLE error:', e);
    }
  }
</script>

<main>
  <nav>
    <span class="brand">Dorky Commander</span>
    <button class:active={activeView === 'scripts'} onclick={() => activeView = 'scripts'}>Scripts</button>
    <button class:active={activeView === 'dbc'} onclick={() => activeView = 'dbc'}>DBC</button>
    <button class:active={activeView === 'dashboard'} onclick={() => activeView = 'dashboard'}>Dashboard</button>
    <button class:active={activeView === 'trace'} onclick={() => activeView = 'trace'}>Trace</button>
    <button class:active={activeView === 'settings'} onclick={() => activeView = 'settings'}>Settings</button>
    {#if connected && fwVersion}
      <span class="fw-version" title="firmware version">fw {fwVersion}</span>
    {/if}
    <button class="connect" class:connected onclick={toggleConnect}>
      {connected ? 'Connected' : 'Connect BLE'}
    </button>
  </nav>

  {#if error}
    <div class="error">{error}</div>
  {/if}
  {#if protoMismatch}
    <div class="warning">{protoMismatch}</div>
  {/if}

  <div class="content">
    {#if activeView === 'scripts'}
      <ScriptsView {proto} {connected} />
    {:else if activeView === 'dbc'}
      <DbcView transport={ble} {proto} {connected} />
    {:else if activeView === 'trace'}
      <TraceView {proto} {connected} />
    {:else if activeView === 'settings'}
      <SettingsView {proto} {connected} />
    {:else}
      <DashboardView transport={ble} {proto} {connected} />
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
  .warning {
    background: #4a3a1a;
    border: 1px solid #a84;
    color: #fc8;
    padding: 0.5rem 0.75rem;
    border-radius: 4px;
    font-size: 0.85rem;
    margin-bottom: 1rem;
  }
  .fw-version {
    color: #888;
    font-size: 0.75rem;
    font-family: monospace;
    margin-right: 0.5rem;
  }
</style>
