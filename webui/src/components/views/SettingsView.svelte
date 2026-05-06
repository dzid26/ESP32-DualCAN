<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { onMount } from 'svelte';

  // System info — refresh on connect.
  let protoVersion = $state<number | null>(null);
  let infoError = $state<string | null>(null);

  async function refreshInfo(): Promise<void> {
    if (!app.connected) return;
    try {
      const info = await app.proto.systemInfo();
      protoVersion = info.proto_version;
      infoError = null;
    } catch (e) {
      infoError = e instanceof Error ? e.message : String(e);
    }
  }

  // WiFi
  let wifiSsid = $state('');
  let wifiPsk = $state('');
  let wifiStatus = $state<{ connected: boolean; ssid: string; ip: string } | null>(null);
  let wifiBusy = $state(false);
  let wifiError = $state<string | null>(null);

  async function refreshWifi(): Promise<void> {
    if (!app.connected) return;
    try {
      wifiStatus = await app.proto.wifiStatus();
      wifiError = null;
    } catch (e) {
      wifiError = e instanceof Error ? e.message : String(e);
    }
  }

  async function saveWifi(): Promise<void> {
    if (!wifiSsid.trim()) { wifiError = 'SSID required'; return; }
    wifiBusy = true;
    wifiError = null;
    try {
      await app.proto.wifiSetCreds(wifiSsid.trim(), wifiPsk);
      wifiPsk = '';
      app.pushLog(`WiFi credentials updated for "${wifiSsid.trim()}"`, 'info', 'wifi');
      setTimeout(refreshWifi, 3000);
    } catch (e) {
      wifiError = e instanceof Error ? e.message : String(e);
    } finally {
      wifiBusy = false;
    }
  }

  onMount(() => { refreshInfo(); refreshWifi(); });
  $effect(() => { if (app.connected) { refreshInfo(); refreshWifi(); } });
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 10px">
  <SectionHead title="Settings" />

  {#if !app.connected}
    <div class="empty">Connect to view live device settings. Form values won't reach the device until you connect.</div>
  {/if}

  <div class="frame">
    <div class="frame__head">Network</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
      {#if wifiStatus}
        <div class="field">
          <span>Status</span>
          <span class="mono ghost">
            {wifiStatus.connected ? 'connected' : 'not connected'}
            {#if wifiStatus.ssid}· ssid {wifiStatus.ssid}{/if}
            {#if wifiStatus.ip}· ip {wifiStatus.ip}{/if}
          </span>
        </div>
      {/if}
      <label class="field">
        <span>WiFi SSID</span>
        <input class="inp" placeholder="home" bind:value={wifiSsid} />
      </label>
      <label class="field">
        <span>Password</span>
        <input type="password" class="inp" bind:value={wifiPsk} placeholder="leave blank for open network" />
      </label>
      <div class="field">
        <span></span>
        <button class="btn btn--info" style="justify-self: start" onclick={saveWifi} disabled={!app.connected || wifiBusy || !wifiSsid.trim()}>
          {wifiBusy ? 'Saving…' : 'Save & connect'}
        </button>
      </div>
      {#if wifiError}<div class="field"><span></span><span class="mono" style="color: var(--dc-err-text); font-size: 11px">{wifiError}</span></div>{/if}
    </div>
  </div>

  <div class="frame">
    <div class="frame__head">Buses</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
      <div class="field">
        <span>Sim mode</span>
        <span class="mono">
          {app.simulation ? 'ON — TX routed to log' : 'OFF — TX hits the bus'}
          <span class="ghost" style="margin-left: 6px">(toggle in status bar)</span>
        </span>
      </div>
      <div class="field">
        <span>Bus 0 bitrate</span>
        <span class="mono ghost">500 kbps · firmware-fixed (not yet exposed by protocol)</span>
      </div>
      <div class="field">
        <span>Bus 1 bitrate</span>
        <span class="mono ghost">500 kbps · firmware-fixed</span>
      </div>
    </div>
  </div>

  <div class="frame">
    <div class="frame__head"><span class="row-flex"><Icon name="sparkle" size={13} />AI assistant</span></div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
      <label class="field">
        <span>Anthropic API key</span>
        <input type="password" class="inp" placeholder="sk-ant-…" />
      </label>
      <div class="field">
        <span></span>
        <span class="muted" style="font-size: 11px">
          Stored locally in your browser. Never sent to the device. Use a workspace-scoped key with a budget — calls go directly from your browser to api.anthropic.com.
        </span>
      </div>
      <label class="field">
        <span>Model</span>
        <select class="sel">
          <option value="haiku-4.5">claude-haiku-4-5 · fast, cheap</option>
          <option value="sonnet-4.5">claude-sonnet-4-5 · stronger reasoning</option>
        </select>
      </label>
      <label class="field">
        <span>Inline completions</span>
        <label class="row-flex" style="gap: 6px">
          <input type="checkbox" checked />
          <span style="font-size: 12px">Ghost-text suggestions in the editor (Tab to accept)</span>
        </label>
      </label>
      <div class="field">
        <span>Confirm vehicle writes</span>
        <label class="row-flex" style="gap: 6px">
          <input type="checkbox" checked disabled />
          <span style="font-size: 12px; color: var(--dc-text-fade)">Always on — agent never auto-fires events that touch the bus</span>
        </label>
      </div>
      <label class="field">
        <span>Block while moving</span>
        <label class="row-flex" style="gap: 6px">
          <input type="checkbox" checked />
          <span style="font-size: 12px">Refuse vehicle writes when speed signal &gt; 5 mph</span>
        </label>
      </label>
      <div class="field">
        <span>Status</span>
        <span class="mono ghost">no key set — paste above to enable</span>
      </div>
    </div>
  </div>

  <div class="frame">
    <div class="frame__head">Firmware</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
      <div class="field">
        <span>Installed</span>
        {#if app.connected && app.fwVersion}
          <span class="mono">{app.fwVersion}{protoVersion !== null ? ` · proto v${protoVersion}` : ''}</span>
        {:else}
          <span class="mono ghost">{infoError ?? 'not connected'}</span>
        {/if}
      </div>
      {#if app.protoMismatch}
        <div class="field">
          <span></span>
          <span class="mono" style="color: var(--dc-warn); font-size: 11px">{app.protoMismatch}</span>
        </div>
      {/if}
      <div class="field">
        <span>Update</span>
        <button class="btn btn--info" style="justify-self: start" disabled title="OTA upload not yet wired in protocol">
          <Icon name="up" size={13} />Upload .bin
        </button>
      </div>
      <div class="field">
        <span></span>
        <a href="https://github.com/dzid26/ESP32-DualCAN/releases" target="_blank" rel="noopener" class="btn btn--sm" style="justify-self: start; text-decoration: none">
          Check GitHub releases
        </a>
      </div>
    </div>
  </div>

  <div class="frame">
    <div class="frame__head" style="color: var(--dc-err-text)">Danger zone</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
      <div class="field">
        <span>Factory reset</span>
        <button class="btn btn--sm btn--danger" style="justify-self: start" disabled title="Not yet wired in protocol">
          Erase NVS + scripts
        </button>
      </div>
    </div>
  </div>
</div>

<style>
  .field {
    display: grid; grid-template-columns: 140px 1fr; gap: 8px;
    align-items: center; font-size: 12px; color: var(--dc-text-dim);
  }
</style>
