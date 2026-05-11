<script lang="ts">
  import { app } from '../lib/store.svelte';
  import Icon from './Icon.svelte';
  import VersionPip from './VersionPip.svelte';
  import BusPip from './BusPip.svelte';
  import CarPip from './CarPip.svelte';
  import { onMount } from 'svelte';

  let { onPalette }: { onPalette: () => void } = $props();

  let latestStableVersion: string | null = $state(null);

  const connState = $derived(
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
    connState === 'ok' ? 'pip__dot--ok' :
    connState === 'err' ? 'pip__dot--err' : 'pip__dot--warn'
  );

  const fwLatestStatus = $derived(
    app.protoMismatch ? 'mismatch' :
    !latestStableVersion || !app.fwVersion ? true :
    compareVersions(app.fwVersion, latestStableVersion) >= 0
  );

  function compareVersions(current: string, latest: string): number {
    const parse = (v: string) => {
      const match = v.match(/v?(\d+)\.(\d+)\.(\d+)/);
      return match ? [parseInt(match[1]), parseInt(match[2]), parseInt(match[3])] : [0, 0, 0];
    };
    const [cm, cn, cp] = parse(current);
    const [lm, ln, lp] = parse(latest);
    return cm !== lm ? cm - lm : cn !== ln ? cn - ln : cp - lp;
  }

  async function checkLatestStable() {
    try {
      const resp = await fetch('https://api.github.com/repos/dzid26/ESP32-DualCAN/releases');
      if (!resp.ok) return;
      const releases: any[] = await resp.json();
      const stable = releases.find((r: any) => !r.prerelease);
      if (stable) latestStableVersion = stable.tag_name;
    } catch {
      /* ignore */
    }
  }

  onMount(() => { checkLatestStable(); });
  $effect(() => {
    if (app.connected) checkLatestStable();
  });
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

  <div class="bus-group">
    <BusPip id={0} name="VehicleCAN" active={app.bus0} rate="488 f/s" />
    <BusPip id={1} name="ChassisCAN" active={app.bus1} rate="122 f/s" />
  </div>

  <span class="spacer"></span>

  <button class="btn btn--sm btn--ghost" onclick={onPalette} title="Cmd-K">
    <Icon name="search" size={14} /><span>⌘K</span>
  </button>

  {#if app.connected}
    <VersionPip
      version={app.fwVersion ?? 'v?.?.?'}
      latest={fwLatestStatus}
      channel="stable"
      progress={app.otaBusy || app.otaDone ? app.otaProgress : null}
      onclick={() => { app.setView('settings'); setTimeout(() => document.getElementById('firmware-frame')?.scrollIntoView({ behavior: 'smooth', block: 'start' }), 50); }}
    />
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
  {/if}
</div>
