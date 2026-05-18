<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { AlertDialog } from 'bits-ui';
  import { onMount } from 'svelte';
  import { toast } from '../../lib/toast.svelte';

  type TeslaStatus = { has_key: boolean; public_key_hex?: string; vin?: string };
  type ScanDevice  = { addr: string; name: string; rssi: number };

  let status       = $state<TeslaStatus | null>(null);
  let busy         = $state(false);
  let scanning     = $state(false);
  let pairing      = $state(false);
  let devices      = $state<ScanDevice[]>([]);
  let err          = $state<string | null>(null);
  let confirmReset = $state(false);
  let vinInput     = $state('');
  let vinSaving    = $state(false);

  async function refresh(): Promise<void> {
    if (!app.connected) return;
    try {
      status = await app.proto.teslaStatus();
      vinInput = status.vin ?? '';
      err = null;
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    }
  }

  async function genKey(): Promise<void> {
    busy = true; err = null;
    try {
      status = await app.proto.teslaGenKey();
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    } finally { busy = false; }
  }

  async function resetKey(): Promise<void> {
    busy = true; err = null; confirmReset = false;
    try {
      await app.proto.teslaReset();
      status = await app.proto.teslaStatus();
      vinInput = '';
      devices = [];
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    } finally { busy = false; }
  }

  async function saveVin(): Promise<void> {
    if (vinInput.length !== 17) { err = 'VIN must be exactly 17 characters'; return; }
    vinSaving = true; err = null;
    try {
      status = await app.proto.teslaSetVin(vinInput.trim().toUpperCase());
      toast.show({ severity: 'info', message: 'VIN saved', duration: 2500 });
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    } finally { vinSaving = false; }
  }

  async function scan(): Promise<void> {
    scanning = true; devices = []; err = null;
    try {
      const res = await app.proto.teslaScan(6000);
      devices = res.devices;
      if (devices.length === 0)
        toast.show({ severity: 'info', message: 'No Tesla found — is the car in range and awake?', duration: 5000 });
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    } finally { scanning = false; }
  }

  async function pair(device: ScanDevice): Promise<void> {
    pairing = true; err = null;
    toast.show({ severity: 'info',
      message: `Connecting to ${device.name || device.addr}…`,
      duration: 35000 });
    try {
      await app.proto.teslaPair(device.addr);
      toast.show({ severity: 'info', duration: 0,
        message: '✓ Whitelist request sent — tap your Tesla keycard or phone on the car\'s NFC reader within 30 s to approve.' });
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      toast.show({ severity: 'error', message: `Pair failed: ${msg}`, duration: 10000 });
      err = msg;
    } finally { pairing = false; }
  }

  async function copyPubkey(): Promise<void> {
    if (!status?.public_key_hex) return;
    try {
      await navigator.clipboard.writeText(status.public_key_hex);
      toast.show({ severity: 'info', message: 'Public key copied', duration: 2500 });
    } catch (e) {
      toast.show({ severity: 'error', message: `Copy failed: ${e instanceof Error ? e.message : String(e)}` });
    }
  }

  onMount(refresh);
  $effect(() => { if (app.connected) refresh(); });
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 10px">
  <SectionHead title="Tesla-BLE" sub="Local vehicle control via VCSEC" />

  {#if !app.connected}
    <div class="empty">Connect to manage Tesla pairing.</div>
  {/if}

  <!-- VIN -->
  <div class="frame">
    <div class="frame__head">Vehicle (VIN)</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
      <div class="field">
        <span>VIN</span>
        <div style="display: flex; gap: 6px; align-items: center">
          <input class="mono vin-input" maxlength="17" placeholder="5YJ..." spellcheck="false"
            bind:value={vinInput} disabled={!app.connected || vinSaving} />
          <button class="btn btn--sm btn--info"
            onclick={saveVin}
            disabled={!app.connected || vinSaving || vinInput.length !== 17 || vinInput === (status?.vin ?? '')}>
            {vinSaving ? 'Saving…' : 'Save'}
          </button>
        </div>
      </div>
      {#if status?.vin}
        <div class="field"><span></span>
          <span class="mono" style="color: var(--dc-ok); font-size: 11px">✓ {status.vin}</span>
        </div>
      {/if}
    </div>
  </div>

  <!-- Keypair -->
  <div class="frame">
    <div class="frame__head">Keypair</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
      <div class="field">
        <span>Status</span>
        <span class="mono">
          {#if status?.has_key}
            <span style="color: var(--dc-ok)">P-256 loaded ✓</span>
          {:else if status}
            <span class="ghost">no keypair</span>
          {/if}
        </span>
      </div>

      {#if status?.has_key && status.public_key_hex}
        <div class="field">
          <span>Public key</span>
          <div style="display: flex; flex-direction: column; gap: 4px; min-width: 0">
            <textarea class="mono key-display" rows="3" readonly>{status.public_key_hex}</textarea>
            <div class="row-flex" style="gap: 6px">
              <button class="btn btn--sm" onclick={copyPubkey}>
                <Icon name="copy" size={12} />Copy hex
              </button>
              <span class="muted" style="font-size: 11px">SEC1 uncompressed (65 B)</span>
            </div>
          </div>
        </div>
      {/if}

      <div class="field">
        <span>Actions</span>
        <div class="row-flex" style="gap: 6px; flex-wrap: wrap">
          <button class="btn btn--sm btn--info" onclick={genKey}
            disabled={!app.connected || busy}>
            {busy ? 'Working…' : status?.has_key ? 'Regenerate' : 'Generate key'}
          </button>
          {#if status?.has_key}
            <button class="btn btn--sm btn--danger" onclick={() => (confirmReset = true)}
              disabled={!app.connected || busy}>
              Wipe
            </button>
          {/if}
        </div>
      </div>

      {#if err}
        <div class="field"><span></span>
          <span class="mono" style="color: var(--dc-err-text); font-size: 11px">{err}</span>
        </div>
      {/if}
    </div>
  </div>

  <!-- Scan + Pair -->
  <div class="frame">
    <div class="frame__head">Pair with vehicle</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
      <div style="font-size: 12px; color: var(--dc-text-dim); line-height: 1.5">
        Scan nearby Tesla VCSEC advertisements. After pairing you must physically
        approve the new key inside the car using a keycard or existing phone key.
      </div>

      <div class="row-flex" style="gap: 6px">
        <button class="btn btn--sm btn--info" onclick={scan}
          disabled={!app.connected || scanning || pairing}>
          {scanning ? 'Scanning…' : 'Scan (6 s)'}
        </button>
      </div>

      {#if devices.length > 0}
        <div style="display: flex; flex-direction: column; gap: 6px">
          {#each devices as d}
            <div class="device-row">
              <div style="min-width: 0; flex: 1">
                <div class="mono" style="font-size: 12px">{d.name || '(no name)'}</div>
                <div class="mono ghost" style="font-size: 10px">{d.addr} · {d.rssi} dBm</div>
              </div>
              <button class="btn btn--sm btn--info"
                onclick={() => pair(d)}
                disabled={pairing || scanning || !status?.has_key}
                title={!status?.has_key ? 'Generate a keypair first' : undefined}>
                {pairing ? 'Pairing…' : 'Pair'}
              </button>
            </div>
          {/each}
        </div>
        {#if !status?.has_key}
          <div style="font-size: 11px; color: var(--dc-text-dim)">
            Generate a keypair above to enable pairing.
          </div>
        {/if}
      {/if}
    </div>
  </div>

  <AlertDialog.Root open={confirmReset} onOpenChange={(v) => (confirmReset = v)}>
    <AlertDialog.Portal>
      <AlertDialog.Overlay class="ad-overlay" />
      <AlertDialog.Content class="frame ad-content">
        <div class="frame__head" style="color: var(--dc-err-text)">
          <AlertDialog.Title style="margin: 0; font-size: inherit; font-weight: inherit;">
            Wipe Tesla keypair + VIN?
          </AlertDialog.Title>
          <AlertDialog.Cancel class="btn btn--sm btn--ghost btn--icon" aria-label="Cancel">
            <Icon name="x" size={13} />
          </AlertDialog.Cancel>
        </div>
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
          <AlertDialog.Description style="margin: 0; font-size: 13px; color: var(--dc-text-dim); line-height: 1.5">
            The private key and VIN will be erased from NVS. You'll need to regenerate a key and re-pair with the vehicle.
          </AlertDialog.Description>
          <div class="row-flex" style="justify-content: flex-end; margin-top: 4px">
            <AlertDialog.Cancel class="btn btn--sm btn--ghost">Cancel</AlertDialog.Cancel>
            <AlertDialog.Action class="btn btn--sm btn--danger" onclick={resetKey} disabled={busy}>
              Wipe
            </AlertDialog.Action>
          </div>
        </div>
      </AlertDialog.Content>
    </AlertDialog.Portal>
  </AlertDialog.Root>
</div>

<style>
  .field {
    display: grid; grid-template-columns: 90px 1fr; gap: 8px;
    align-items: start; font-size: 12px; color: var(--dc-text-dim);
  }
  .vin-input {
    width: 200px; font-size: 12px; padding: 4px 8px;
    background: var(--dc-surface-2, #1a1a2e);
    color: var(--dc-text); border: 1px solid var(--dc-border, #333);
    border-radius: 4px;
  }
  .key-display {
    width: 100%; font-size: 11px;
    background: var(--dc-surface-2, #1a1a2e);
    color: var(--dc-text); border: 1px solid var(--dc-border, #333);
    border-radius: 4px; padding: 6px 8px; word-break: break-all; resize: vertical;
  }
  .device-row {
    display: flex; align-items: center; gap: 10px;
    padding: 8px 10px; border-radius: 6px;
    background: var(--dc-surface-2, #1a1a2e);
    border: 1px solid var(--dc-border, #333);
  }
</style>
