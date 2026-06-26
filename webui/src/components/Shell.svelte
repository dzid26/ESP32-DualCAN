<script lang="ts">
  import { app } from '../lib/store.svelte';
  import Rail from './Rail.svelte';
  import StatusStrip from './StatusStrip.svelte';
  import CommandPalette from './CommandPalette.svelte';
  import LogPane from './LogPane.svelte';
  import Icon from './Icon.svelte';
  import type { Snippet } from 'svelte';

  let { children }: { children: Snippet } = $props();

  $effect(() => {
    function onKey(e: KeyboardEvent): void {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        app.paletteOpen = !app.paletteOpen;
      } else if (e.key === 'Escape') {
        app.paletteOpen = false;
      }
    }
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  });

</script>

<div class="shell">
  <Rail onToggleLog={() => (app.logOpen = !app.logOpen)} />

  <div class="shell__main">
    <StatusStrip onPalette={() => (app.paletteOpen = true)} />

    {#if app.killed}
      <div class="stateBanner stateBanner--kill">
        <Icon name="power" size={14} />
        <span>Kill switch engaged · all scripts paused</span>
        <span class="spacer"></span>
        <button class="btn btn--sm btn--ghost" onclick={() => app.toggleKill()}>Release</button>
      </div>
    {/if}
    {#if app.simulation}
      <div class="stateBanner stateBanner--sim">
        <Icon name="sim" size={14} />
        <span>Simulation mode · sends routed to log, nothing hits the wire</span>
        <span class="spacer"></span>
        <button class="btn btn--sm btn--ghost" onclick={() => app.toggleSim()}>Turn off</button>
      </div>
    {/if}

    <main style="flex: 1; display: flex; flex-direction: column; min-height: 0; overflow: hidden">
      {@render children()}
    </main>

    {#if app.logOpen}
      <LogPane />
    {/if}
  </div>
</div>

<CommandPalette open={app.paletteOpen} onClose={() => (app.paletteOpen = false)} />
