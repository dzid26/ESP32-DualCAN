<script lang="ts">
  import { app } from '../lib/store.svelte';
  import { NAV_ITEMS } from '../lib/nav';
  import { modKey, isIOS } from '../lib/platform';
  import Icon from './Icon.svelte';
  import { toast } from '../lib/toast.svelte';

  let { onToggleLog }: { onToggleLog: () => void } = $props();

  // The logo is the only place a phone can be told why a tap did nothing. A
  // browser offers the install prompt only once it has decided the site is
  // installable, and it stops offering it after the app is installed, so the
  // remaining cases are "already installed" and "this browser wants another
  // route".
  function onBrandClick(): void {
    if (app.installPrompt) {
      void app.installApp();
      return;
    }
    if (app.isInstalled) {
      toast.show({ message: 'Dorky is installed. Open it from your home screen.' });
      return;
    }
    toast.show({
      message: isIOS
        ? 'Tap Share, then Add to Home Screen to install.'
        : 'Already installed? Open it from your home screen. Otherwise use Install in the browser menu.',
      duration: 10000,
    });
  }

  $effect(() => {
    if (app.logOpen) app.logAttention = false;
  });
</script>

<aside class="rail">
  {#snippet brandInner()}
    <div class="rail__brand-plate">
      <svg class="rail__brand-icon" viewBox="0 0 24 24" aria-hidden="true">
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
  <button
    class="rail__brand"
    type="button"
    style="cursor: pointer"
    aria-label={app.isInstalled ? 'Dorky Commander' : 'Install app'}
    title={app.isInstalled ? 'Dorky Commander' : 'Install app'}
    onclick={onBrandClick}
  >
    {@render brandInner()}
  </button>

  <div class="rail__list">
    {#each NAV_ITEMS as item}
      {#if item.id !== 'tesla' || app.car?.brand === 'Tesla'}
        <button
          class={'railItem' + (app.view === item.id ? ' railItem--active' : '')}
          onclick={() => app.setView(item.id)}
          title={item.label}
        >
          <Icon name={item.icon} size={20} />
          <span class="railItem__label">{item.label}</span>
          <span class="tt">{item.label}</span>
        </button>
      {/if}
    {/each}
  </div>

  <div class="rail__foot">
    <button class="railItem" class:railItem--attention={app.logAttention} onclick={onToggleLog} title="Log">
      <Icon name="log" size={18} />
      <span class="railItem__label">Log</span>
      <span class="tt">Device log</span>
    </button>
  </div>
</aside>
