<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { onMount } from 'svelte';

  const GITHUB_REPO = 'dzid26/ESP32-DualCAN';

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

  // Device-side Anthropic API key (stored in NVS)
  let deviceKeySet = $state<boolean | null>(null);
  let deviceKeyBusy = $state(false);
  let deviceKeyError = $state<string | null>(null);

  async function refreshDeviceKey(): Promise<void> {
    if (!app.connected) return;
    try {
      const r = await app.proto.getSecret('anthropic');
      deviceKeySet = r.value !== null;
      if (r.value) app.setAiKey(r.value);
    } catch {
      deviceKeySet = false;
    }
  }

  async function saveDeviceKey(): Promise<void> {
    if (!app.aiKey.trim()) { deviceKeyError = 'Paste a key above first'; return; }
    deviceKeyBusy = true;
    deviceKeyError = null;
    try {
      await app.proto.setSecret('anthropic', app.aiKey.trim());
      deviceKeySet = true;
      app.pushLog('Anthropic API key saved to device NVS', 'info', 'settings');
    } catch (e) {
      deviceKeyError = e instanceof Error ? e.message : String(e);
    } finally {
      deviceKeyBusy = false;
    }
  }

  async function clearDeviceKey(): Promise<void> {
    deviceKeyBusy = true;
    deviceKeyError = null;
    try {
      await app.proto.clearSecret('anthropic');
      deviceKeySet = false;
      app.pushLog('Anthropic API key cleared from device NVS', 'info', 'settings');
    } catch (e) {
      deviceKeyError = e instanceof Error ? e.message : String(e);
    } finally {
      deviceKeyBusy = false;
    }
  }

  // GitHub release state
  let ghChecking = $state(false);
  let ghRelease = $state<{ tag: string; name: string; url: string; size: number; published: string } | null>(null);
  let ghError = $state<string | null>(null);
  let ghDownloading = $state(false);

  /** Pick a .bin file from disk and flash it. */
  async function uploadFromFile(): Promise<void> {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.bin';
    input.onchange = async () => {
      const file = input.files?.[0];
      if (!file) return;
      await app.doOTA(new Uint8Array(await file.arrayBuffer()), file.name);
    };
    input.click();
  }

  /** Fetch the latest pre-release from GitHub. */
  async function checkGitHub(): Promise<void> {
    ghChecking = true;
    ghError = null;
    ghRelease = null;
    try {
      const resp = await fetch(`https://api.github.com/repos/${GITHUB_REPO}/releases`);
      if (!resp.ok) throw new Error(`GitHub API ${resp.status}`);
      const releases: any[] = await resp.json();
      // Find the latest prerelease, or fall back to latest release
      const pre = releases.find((r: any) => r.prerelease) ?? releases[0];
      if (!pre) { ghError = 'No releases found'; return; }
      // Find the .bin asset
      const asset = pre.assets?.find((a: any) => a.name.endsWith('.bin'));
      if (!asset) { ghError = `Release ${pre.tag_name} has no .bin asset`; return; }
      ghRelease = {
        tag: pre.tag_name,
        name: pre.name ?? pre.tag_name,
        url: asset.url,
        size: asset.size,
        published: new Date(pre.published_at).toLocaleDateString(),
      };
    } catch (e) {
      ghError = e instanceof Error ? e.message : String(e);
    } finally {
      ghChecking = false;
    }
  }

  /** Download firmware from GitHub and flash it. */
  async function downloadAndFlash(): Promise<void> {
    if (!ghRelease) return;
    ghDownloading = true;
    ghError = null;
    try {
      app.otaStatus = 'Downloading from GitHub…';
      const resp = await fetch(ghRelease.url, {
        headers: { 'Accept': 'application/octet-stream' }
      });
      if (!resp.ok) throw new Error(`Download failed: ${resp.status}`);
      const buf = new Uint8Array(await resp.arrayBuffer());
      ghDownloading = false;
      await app.doOTA(buf, `${ghRelease.tag}.bin`);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      // CORS errors on GitHub CDN — fall back to download link + manual upload
      if (msg.includes('CORS') || msg.includes('Failed to fetch')) {
        ghError = `Browser can't access CDN. Use Download button below, then Upload .bin.`;
      } else {
        ghError = msg;
      }
      ghDownloading = false;
    }
  }

  onMount(() => { refreshInfo(); refreshWifi(); refreshDeviceKey(); });
  $effect(() => {
    if (app.connected) {
      refreshInfo();
      refreshWifi();
      refreshDeviceKey();
    }
  });
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 10px">
  <SectionHead title="Settings" />

  {#if !app.connected}
    <div class="empty">Connect to view live device settings. Form values won't reach the device until you connect.</div>
  {/if}

  {#if app.installPrompt && !app.isInstalled}
    <div class="frame">
      <div class="frame__head">Install app</div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
        <div class="field">
          <span>Add to home screen</span>
          <div style="display: flex; flex-direction: column; gap: 4px; align-items: flex-start">
            <button class="btn btn--info btn--sm" onclick={() => app.installApp()}>
              <Icon name="down" size={13} />Install
            </button>
            <span class="muted" style="font-size: 11px">
              Opens and connects faster than a browser tab. Works offline for the UI.
            </span>
          </div>
        </div>
      </div>
    </div>
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
      <div class="field">
        <span>Anthropic API key</span>
        <div style="display: flex; flex-direction: column; gap: 4px">
          <span style="font-size: 11px; color: var(--dc-text-fade)">
            Stored in browser + device NVS. Loaded from device on connect (BLE encrypted). Calls go directly from browser to api.anthropic.com.
            <a href="https://platform.claude.com/settings/keys" target="_blank" rel="noopener"
              style="color: var(--dc-accent); text-decoration: none">Get key ↗</a>
          </span>
          <input type="password" class="inp" placeholder="sk-ant-…"
            value={app.aiKey}
            oninput={(e) => app.setAiKey((e.target as HTMLInputElement).value)} />
          <div style="display: flex; align-items: center; gap: 6px; flex-wrap: wrap">
            <button class="btn btn--sm" onclick={saveDeviceKey}
              disabled={!app.connected || deviceKeyBusy || !app.aiKey.trim()}>
              {deviceKeyBusy ? 'Saving…' : 'Save to device'}
            </button>
            <button class="btn btn--sm btn--danger" onclick={clearDeviceKey}
              disabled={!app.connected || deviceKeyBusy || deviceKeySet !== true}>
              Clear
            </button>
            <span style="font-size: 11px; color: var(--dc-text-fade)">
              {#if deviceKeySet === true}<span style="color: var(--dc-ok)">saved on device ✓</span>{:else if deviceKeySet === false}not on device{:else if app.connected}checking…{/if}
            </span>
            {#if deviceKeyError}<span class="mono" style="font-size: 11px; color: var(--dc-err-text)">{deviceKeyError}</span>{/if}
          </div>
        </div>
      </div>

      <div class="field">
        <span>Model</span>
        <div style="display: flex; flex-direction: column; gap: 4px">
          <select class="sel" value={app.aiModel} onchange={(e) => app.setAiModel((e.target as HTMLSelectElement).value)}>
            <option value="claude-haiku-4-5-20251001">claude-haiku-4-5 · fast, cheap</option>
            <option value="claude-sonnet-4-6">claude-sonnet-4-6 · stronger reasoning</option>
          </select>
          <a href="https://platform.claude.com/usage" target="_blank" rel="noopener"
            style="font-size: 11px; color: var(--dc-accent); text-decoration: none">Token usage ↗</a>
        </div>
      </div>

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
    </div>
  </div>

  <div class="frame" id="firmware-frame">
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
        <span>Update firmware</span>
        <div style="display: flex; gap: 6px; flex-wrap: wrap; align-items: center">
          <button
            id="ota-upload-btn"
            class="btn btn--info btn--sm"
            onclick={uploadFromFile}
            disabled={!app.connected || app.otaBusy}
          >
            <Icon name="up" size={13} />Upload .bin
          </button>
          <button
            id="ota-github-check-btn"
            class="btn btn--sm"
            onclick={checkGitHub}
            disabled={ghChecking || app.otaBusy}
          >
            {ghChecking ? 'Checking…' : 'Check GitHub Releases'}
          </button>
        </div>
      </div>

      {#if ghRelease || ghError}
        <div class="field">
          <span></span>
          <div style="display: flex; flex-direction: column; gap: 6px; align-items: flex-start">
            {#if ghRelease}
              <div class="ota-release-card">
                <div class="ota-release-row">
                  <span class="mono" style="font-weight: 600; color: var(--dc-accent)">{ghRelease.tag}</span>
                  <span class="ghost" style="font-size: 10px">{ghRelease.published}</span>
                </div>
                <div style="font-size: 11px; color: var(--dc-text-dim)">{ghRelease.name} · {(ghRelease.size / 1024).toFixed(0)} KB</div>
                <div style="display: flex; gap: 6px; margin-top: 4px">
                  <button
                    id="ota-github-flash-btn"
                    class="btn btn--info btn--sm"
                    onclick={downloadAndFlash}
                    disabled={!app.connected || app.otaBusy || ghDownloading}
                  >
                    {ghDownloading ? 'Downloading…' : 'Download & flash'}
                  </button>
                  <a
                    href={`https://github.com/dzid26/ESP32-DualCAN/releases/download/${ghRelease.tag}/dorky-commander-${ghRelease.tag.slice(1)}.bin`}
                    class="btn btn--sm"
                    download
                    title="Download .bin file to upload manually"
                  >
                    Download
                  </a>
                </div>
              </div>
            {/if}
            {#if ghError}
              <span class="mono" style="color: var(--dc-err-text); font-size: 11px">{ghError}</span>
            {/if}
          </div>
        </div>
      {/if}

      <div class="field">
        <span>After update</span>
        <label style="display: inline-flex; align-items: center; gap: 6px; cursor: pointer; font-size: 11px; color: var(--dc-text-dim)">
          <input
            type="checkbox"
            bind:checked={app.rebootAfterUpdate}
            disabled={!app.connected}
          />
          Reboot automatically
        </label>
      </div>

      <!-- OTA progress -->
      {#if app.otaBusy || app.otaDone}
        <div class="field">
          <span>Flash progress</span>
          <div style="display: flex; flex-direction: column; gap: 6px; width: 100%">
            <div class="ota-progress-wrap">
              <div class="ota-progress-bar" style="width: {app.otaProgress}%"></div>
            </div>
            <div style="display: flex; align-items: center; gap: 8px">
              <span class="mono" style="font-size: 11px; color: {app.otaDone ? 'var(--dc-ok)' : 'var(--dc-text-dim)'}">
                {app.otaStatus}
              </span>
              {#if app.otaBusy}
                <button class="btn btn--sm btn--danger" onclick={() => app.abortOTA()}>Abort</button>
              {/if}
            </div>
          </div>
        </div>
      {/if}

      {#if app.otaError}
        <div class="field">
          <span></span>
          <span class="mono" style="color: var(--dc-err-text); font-size: 11px">{app.otaError}</span>
        </div>
      {/if}

      <div class="field">
        <span>System power</span>
        <button
          class={'btn btn--sm ' + (app.otaDone ? 'btn--info' : 'btn--danger')}
          style="justify-self: start"
          onclick={() => app.rebootDevice()}
          disabled={!app.connected || app.otaBusy}
        >
          <Icon name="power" size={12} />
          <span>{app.otaDone ? 'Reboot now' : 'Reboot device'}</span>
        </button>
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

  .ota-progress-wrap {
    width: 100%;
    height: 6px;
    border-radius: 3px;
    background: var(--dc-bg-2, #222);
    overflow: hidden;
  }
  .ota-progress-bar {
    height: 100%;
    border-radius: 3px;
    background: var(--dc-accent, #6e8efb);
    transition: width 0.15s ease;
  }

  .ota-release-card {
    background: var(--dc-bg-2, #1a1a2e);
    border: 1px solid var(--dc-border, #333);
    border-radius: 6px;
    padding: 8px 10px;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .ota-release-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
</style>

