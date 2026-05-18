<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { AlertDialog } from 'bits-ui';
  import { onMount } from 'svelte';
  import { toast } from '../../lib/toast.svelte';

  let status = $state<{ has_key: boolean; public_key_hex?: string } | null>(null);
  let busy = $state(false);
  let err = $state<string | null>(null);
  let confirmReset = $state(false);

  async function refresh(): Promise<void> {
    if (!app.connected) return;
    try {
      status = await app.proto.teslaStatus();
      err = null;
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    }
  }

  async function genKey(): Promise<void> {
    busy = true; err = null;
    try {
      status = await app.proto.teslaGenKey();
      app.pushLog('Tesla BLE keypair generated', 'info', 'tesla');
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }

  async function resetKey(): Promise<void> {
    busy = true; err = null;
    confirmReset = false;
    try {
      await app.proto.teslaReset();
      status = await app.proto.teslaStatus();
      app.pushLog('Tesla BLE keypair wiped', 'warn', 'tesla');
    } catch (e) {
      err = e instanceof Error ? e.message : String(e);
    } finally {
      busy = false;
    }
  }

  async function copyPubkey(): Promise<void> {
    if (!status?.public_key_hex) return;
    try {
      await navigator.clipboard.writeText(status.public_key_hex);
      toast.show({ severity: 'info', message: 'Public key copied to clipboard', duration: 3000 });
    } catch (e) {
      toast.show({ severity: 'error', message: `Copy failed: ${e instanceof Error ? e.message : String(e)}` });
    }
  }

  onMount(refresh);
  $effect(() => { if (app.connected) refresh(); });
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 10px">
  <SectionHead title="Tesla-BLE" sub="Local control of a Tesla via its BLE VCSEC service" />

  {#if !app.connected}
    <div class="empty">Connect to manage the Tesla pairing keypair.</div>
  {/if}

  <div class="frame">
    <div class="frame__head">Keypair</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
      {#if status}
        <div class="field">
          <span>Status</span>
          <span class="mono">
            {#if status.has_key}
              <span style="color: var(--dc-ok)">P-256 keypair loaded ✓</span>
            {:else}
              <span class="ghost">no keypair</span>
            {/if}
          </span>
        </div>
      {/if}

      {#if status?.has_key && status.public_key_hex}
        <div class="field">
          <span>Public key</span>
          <div style="display: flex; flex-direction: column; gap: 4px; min-width: 0">
            <textarea class="mono key-display" rows="3" readonly>{status.public_key_hex}</textarea>
            <div class="row-flex" style="gap: 6px">
              <button class="btn btn--sm" onclick={copyPubkey}>
                <Icon name="copy" size={12} />Copy hex
              </button>
              <span class="muted" style="font-size: 11px">SEC1 uncompressed (65 bytes)</span>
            </div>
          </div>
        </div>
      {/if}

      <div class="field">
        <span>Actions</span>
        <div class="row-flex" style="gap: 6px; flex-wrap: wrap">
          <button class="btn btn--sm btn--info" onclick={genKey}
            disabled={!app.connected || busy}>
            {busy ? 'Working…' : status?.has_key ? 'Regenerate key' : 'Generate key'}
          </button>
          {#if status?.has_key}
            <button class="btn btn--sm btn--danger" onclick={() => (confirmReset = true)}
              disabled={!app.connected || busy}>
              Wipe key
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

  <div class="frame">
    <div class="frame__head">What this does today</div>
    <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px; font-size: 12px; color: var(--dc-text-dim); line-height: 1.5">
      <p style="margin: 0">
        Generates and persists the P-256 keypair Tesla's VCSEC service needs to authenticate this device.
        <strong style="color: var(--dc-text)">The actual BLE handshake with the car is not yet wired</strong> — the next step is to integrate
        <a href="https://github.com/yoziru/tesla-ble" target="_blank" rel="noopener" style="color: var(--dc-accent)">yoziru/tesla-ble</a>
        as a NimBLE central, sign commands with this key, and expose <span class="mono">tesla.wake / unlock / charge_*</span> ops.
      </p>
      <p style="margin: 0">
        See <a href="https://github.com/dzid26/ESP32-DualCAN/blob/main/docs/tesla-ble.md" target="_blank" rel="noopener" style="color: var(--dc-accent)">docs/tesla-ble.md</a>
        for the full plan.
      </p>
    </div>
  </div>

  <AlertDialog.Root open={confirmReset} onOpenChange={(v) => (confirmReset = v)}>
    <AlertDialog.Portal>
      <AlertDialog.Overlay class="ad-overlay" />
      <AlertDialog.Content class="frame ad-content">
        <div class="frame__head" style="color: var(--dc-err-text)">
          <AlertDialog.Title style="margin: 0; font-size: inherit; font-weight: inherit;">
            Wipe Tesla keypair?
          </AlertDialog.Title>
          <AlertDialog.Cancel class="btn btn--sm btn--ghost btn--icon" aria-label="Cancel">
            <Icon name="x" size={13} />
          </AlertDialog.Cancel>
        </div>
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
          <AlertDialog.Description style="margin: 0; font-size: 13px; color: var(--dc-text-dim); line-height: 1.5">
            The private key will be erased from NVS. The corresponding public key will no longer be valid for any car you've paired it with.
          </AlertDialog.Description>
          <p style="margin: 0; font-size: 12px; color: var(--dc-text-fade); line-height: 1.45">
            You'll need to generate a fresh key and re-pair with the vehicle from inside the car.
          </p>
          <div class="row-flex" style="justify-content: flex-end; margin-top: 4px">
            <AlertDialog.Cancel class="btn btn--sm btn--ghost">Cancel</AlertDialog.Cancel>
            <AlertDialog.Action class="btn btn--sm btn--danger" onclick={resetKey} disabled={busy}>
              Wipe key
            </AlertDialog.Action>
          </div>
        </div>
      </AlertDialog.Content>
    </AlertDialog.Portal>
  </AlertDialog.Root>
</div>

<style>
  .field {
    display: grid; grid-template-columns: 110px 1fr; gap: 8px;
    align-items: start; font-size: 12px; color: var(--dc-text-dim);
  }
  .key-display {
    width: 100%;
    font-size: 11px;
    background: var(--dc-surface-2, #1a1a2e);
    color: var(--dc-text);
    border: 1px solid var(--dc-border, #333);
    border-radius: 4px;
    padding: 6px 8px;
    word-break: break-all;
    resize: vertical;
  }
</style>
