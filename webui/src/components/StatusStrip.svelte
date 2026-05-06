<script lang="ts">
  import { app } from '../lib/store.svelte';
  import Icon from './Icon.svelte';
  import VersionPip from './VersionPip.svelte';
  import BusPip from './BusPip.svelte';
  import CarPip from './CarPip.svelte';

  let { onPalette }: { onPalette: () => void } = $props();

  const state = $derived(
    app.connecting ? 'connecting' :
    app.connected ? 'ok' : 'err'
  );
  const stateLabel = $derived(
    app.connecting ? 'Connecting…' :
    app.connected
      ? (app.transport === 'ws' ? 'WiFi · dorky.local' : 'BLE · DorkyCmdr-7F2A')
      : 'Not connected'
  );
  const dotClass = $derived(
    state === 'ok' ? 'pip__dot--ok' :
    state === 'err' ? 'pip__dot--err' : 'pip__dot--warn'
  );
</script>

<div class="status">
  <button class="pip pip--clickable" onclick={() => app.toggleConnect()} title="Toggle connection">
    {#if app.transport === 'ws'}
      <Icon name="wifi" size={14} />
    {:else}
      <Icon name="ble" size={14} />
    {/if}
    <span class={'pip__dot ' + dotClass}></span>
    <span>{stateLabel}</span>
  </button>

  <CarPip car={app.car} onOpen={() => (app.carPickerOpen = true)} />

  <BusPip id={0} name="VehicleCAN" active={app.bus0} rate="488 f/s" />
  <BusPip id={1} name="ChassisCAN" active={app.bus1} rate="122 f/s" />

  <span class="spacer"></span>

  <VersionPip version="v0.3.1" latest={true} channel="stable" />

  <button class="btn btn--sm btn--ghost" onclick={onPalette} title="Cmd-K">
    <Icon name="search" size={14} /><span>⌘K</span>
  </button>
  <button
    class={'btn btn--sm' + (app.simulation ? ' btn--info' : '')}
    onclick={() => app.toggleSim()}
    title="Simulation mode — all sends routed to the log instead of CAN bus"
  >
    <Icon name="sim" size={14} /><span>Sim</span>
  </button>
  <button
    class={'btn btn--sm' + (app.killed ? ' btn--danger' : '')}
    onclick={() => app.toggleKill()}
    title="Disable ALL scripts — fail-safe"
  >
    <Icon name="power" size={14} /><span>{app.killed ? 'Release' : 'Kill'}</span>
  </button>
</div>
