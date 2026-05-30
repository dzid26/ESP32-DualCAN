<script lang="ts">
  import { app } from '../lib/store.svelte';
  import { NAV_ITEMS } from '../lib/nav';
  import { modKey, isTouch } from '../lib/platform';
  import Icon from './Icon.svelte';

  let { onPalette, onToggleLog }: { onPalette: () => void; onToggleLog: () => void } = $props();

  const canInstall = $derived(!isTouch && !!app.installPrompt && !app.isInstalled);

  $effect(() => {
    if (app.logOpen) app.logAttention = false;
  });
</script>

<aside class="rail">
  {#snippet brandInner()}
    <div class="rail__brand-plate">
      <svg width="20" height="20" viewBox="0 0 24 24" aria-hidden="true">
        <rect x="4" y="4" width="16" height="16" rx="2" fill="none" stroke="var(--dc-accent)" stroke-width="1.4" />
        <circle cx="12" cy="12" r="2.2" fill="var(--dc-accent)" />
        <path d="M2 12h2M20 12h2M12 2v2M12 20v2" stroke="var(--dc-text-fade)" stroke-width="1" stroke-linecap="round" />
      </svg>
    </div>
    <div class="rail__brand-text">
      <div class="rail__brand-name mono">DORKY</div>
      <div class="rail__brand-name mono">CMDR</div>
    </div>
  {/snippet}
  {#if canInstall}
    <button
      class="rail__brand"
      type="button"
      style="cursor: pointer"
      aria-label="Install app"
      title="Install app"
      onclick={() => app.installApp()}
    >
      {@render brandInner()}
    </button>
  {:else}
    <div
      class="rail__brand"
      aria-label="Dorky Commander"
      title="Dorky Commander · CAN reverse-engineering tool"
    >
      {@render brandInner()}
    </div>
  {/if}

  <div class="rail__list">
    {#each NAV_ITEMS as item}
      {#if item.id !== 'tesla' || app.car?.brand === 'Tesla'}
        <button
          class={'railItem' + (app.view === item.id ? ' railItem--active' : '')}
          onclick={() => app.setView(item.id)}
        >
          <Icon name={item.icon} size={20} />
          <span class="railItem__label">{item.label}</span>
          <span class="tt">{item.label}</span>
        </button>
      {/if}
    {/each}
  </div>

  <div class="rail__foot">
    <button class="railItem" onclick={onPalette} title="Command palette ({modKey}K)">
      <Icon name="search" size={18} />
      <span class="railItem__label">Search</span>
      <span class="tt">Search / commands{isTouch ? '' : ` · ${modKey}K`}</span>
    </button>
    <button class="railItem" class:railItem--attention={app.logAttention} onclick={onToggleLog} title="Logs">
      <Icon name="log" size={18} />
      <span class="railItem__label">Logs</span>
      <span class="tt">Device logs</span>
    </button>
  </div>
</aside>
