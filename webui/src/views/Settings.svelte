<script lang="ts">
  import type { Protocol } from '../transport/protocol';
  import { onMount } from 'svelte';

  let { proto, connected }: { proto: Protocol; connected: boolean } = $props();

  // ---- System info ----
  let fwVersion = $state<string | null>(null);
  let protoVersion = $state<number | null>(null);
  let infoError = $state<string | null>(null);

  async function refreshInfo() {
    if (!connected) return;
    try {
      const info = await proto.systemInfo();
      fwVersion = info.fw_version;
      protoVersion = info.proto_version;
      infoError = null;
    } catch (e: any) {
      infoError = e?.message ?? String(e);
    }
  }

  // ---- Simulation mode ----
  let simEnabled = $state(false);
  let simBusy = $state(false);
  let simError = $state<string | null>(null);

  async function toggleSim(checked: boolean) {
    simBusy = true;
    simError = null;
    try {
      await proto.setSimMode(checked);
      simEnabled = checked;
    } catch (e: any) {
      simError = e?.message ?? String(e);
    } finally {
      simBusy = false;
    }
  }

  // ---- WiFi ----
  let wifiSsid = $state('');
  let wifiPsk = $state('');
  let wifiStatus = $state<{ connected: boolean; ssid: string; ip: string } | null>(null);
  let wifiBusy = $state(false);
  let wifiError = $state<string | null>(null);

  async function refreshWifi() {
    if (!connected) return;
    try {
      wifiStatus = await proto.wifiStatus();
      wifiError = null;
    } catch (e: any) {
      wifiError = e?.message ?? String(e);
    }
  }

  async function saveWifi() {
    if (!wifiSsid.trim()) { wifiError = 'SSID required'; return; }
    wifiBusy = true;
    wifiError = null;
    try {
      await proto.wifiSetCreds(wifiSsid.trim(), wifiPsk);
      wifiPsk = '';   // don't keep secrets in the form
      // Allow a couple of seconds for the device to attempt the connect.
      setTimeout(refreshWifi, 3000);
    } catch (e: any) {
      wifiError = e?.message ?? String(e);
    } finally {
      wifiBusy = false;
    }
  }

  // ---- Kill switch: disable every loaded script ----
  let killBusy = $state(false);
  let killStatus = $state<string | null>(null);

  async function killAll() {
    if (!confirm('Disable every script currently loaded on the device?')) return;
    killBusy = true;
    killStatus = 'listing scripts...';
    try {
      const r = await proto.listScripts();
      const enabled = r.scripts.filter(s => s.enabled);
      if (enabled.length === 0) { killStatus = 'no scripts were enabled'; return; }
      let n = 0;
      for (const s of enabled) {
        killStatus = `disabling ${s.filename} (${++n}/${enabled.length})...`;
        try { await proto.disableScript(s.filename); } catch (e: any) {
          killStatus = `failed on ${s.filename}: ${e?.message ?? e}`;
          return;
        }
      }
      killStatus = `disabled ${enabled.length} script(s)`;
    } catch (e: any) {
      killStatus = `list failed: ${e?.message ?? e}`;
    } finally {
      killBusy = false;
    }
  }

  onMount(() => { refreshInfo(); refreshWifi(); });
  $effect(() => { if (connected) { refreshInfo(); refreshWifi(); } });
</script>

<div class="settings">
  <h2>Settings</h2>

  {#if !connected}
    <div class="placeholder">
      Connect to the device to view and change settings.
    </div>
  {:else}

    <section>
      <h3>Device</h3>
      {#if infoError}
        <div class="err">{infoError}</div>
      {:else}
        <dl>
          <dt>Firmware</dt><dd>{fwVersion ?? '…'}</dd>
          <dt>Protocol</dt><dd>v{protoVersion ?? '…'}</dd>
        </dl>
      {/if}
    </section>

    <section>
      <h3>Simulation mode</h3>
      <p class="hint">When ON, every <code>can_send</code> is routed to the log instead of the bus. Use to develop scripts without being in the car.</p>
      <label class="toggle">
        <input
          type="checkbox"
          checked={simEnabled}
          disabled={simBusy}
          onchange={(e) => toggleSim((e.currentTarget as HTMLInputElement).checked)}
        />
        <span>{simEnabled ? 'ON — TX is logged' : 'OFF — TX hits the bus'}</span>
      </label>
      {#if simError}<div class="err small">{simError}</div>{/if}
    </section>

    <section>
      <h3>WiFi</h3>
      <p class="hint">Set credentials to connect the device to your network. Status updates a few seconds after save.</p>
      {#if wifiStatus}
        <dl>
          <dt>Status</dt>
          <dd>{wifiStatus.connected ? 'connected' : 'not connected'}</dd>
          {#if wifiStatus.ssid}<dt>SSID</dt><dd>{wifiStatus.ssid}</dd>{/if}
          {#if wifiStatus.ip}<dt>IP</dt><dd>{wifiStatus.ip}</dd>{/if}
        </dl>
      {/if}
      <div class="wifi-form">
        <input bind:value={wifiSsid} placeholder="SSID" autocomplete="off" />
        <input bind:value={wifiPsk} type="password" placeholder="password (blank for open)" autocomplete="off" />
        <button onclick={saveWifi} disabled={wifiBusy}>Save &amp; connect</button>
      </div>
      {#if wifiError}<div class="err small">{wifiError}</div>{/if}
    </section>

    <section>
      <h3>Kill switch</h3>
      <p class="hint">Disables every currently-enabled script in one shot. Use if a script is misbehaving.</p>
      <button class="danger" onclick={killAll} disabled={killBusy}>Disable all scripts</button>
      {#if killStatus}<div class="status">{killStatus}</div>{/if}
    </section>

  {/if}
</div>

<style>
  section { margin: 1.25rem 0; }
  h3 { margin: 0 0 0.25rem; }
  .placeholder {
    background: #0d0d1a;
    border: 1px dashed #333;
    border-radius: 4px;
    padding: 2rem;
    text-align: center;
    color: #666;
  }
  .hint { color: #888; font-size: 0.85rem; margin: 0 0 0.5rem; }
  dl {
    display: grid;
    grid-template-columns: max-content 1fr;
    gap: 0.25rem 0.75rem;
    font-size: 0.9rem;
    margin: 0;
  }
  dt { color: #888; }
  dd { margin: 0; font-family: monospace; color: #ccc; }
  .toggle {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    color: #bbb;
    font-size: 0.9rem;
    cursor: pointer;
  }
  .toggle input[type="checkbox"] {
    width: 1rem;
    height: 1rem;
    accent-color: #d8b94c;
  }
  button {
    background: #2a2a4a;
    border: 1px solid #444;
    color: #ccc;
    padding: 0.4rem 1rem;
    border-radius: 4px;
    cursor: pointer;
  }
  .wifi-form {
    display: flex;
    gap: 0.4rem;
    margin-top: 0.4rem;
    flex-wrap: wrap;
  }
  .wifi-form input {
    flex: 1;
    min-width: 12em;
    background: #0d0d1a;
    border: 1px solid #333;
    border-radius: 4px;
    color: #ddd;
    padding: 0.4rem 0.6rem;
    font-size: 0.85rem;
  }
  button.danger { background: #4a1a1a; border-color: #a44; color: #faa; }
  button:disabled { opacity: 0.4; cursor: not-allowed; }
  .err { color: #f55; margin-top: 0.4rem; }
  .err.small { font-size: 0.8rem; }
  .status { color: #888; font-size: 0.85rem; margin-top: 0.4rem; }
</style>
