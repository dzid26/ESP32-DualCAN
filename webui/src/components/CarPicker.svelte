<script lang="ts">
  import { OPENDBC_CARS, type Car } from '../lib/cars';
  import Icon from './Icon.svelte';

  let { open, current, onPick, onClear, onClose }: {
    open: boolean;
    current: Car | null;
    onPick: (c: Car) => void;
    onClear: () => void;
    onClose: () => void;
  } = $props();

  let q = $state('');
  let inputEl: HTMLInputElement | undefined = $state();

  $effect(() => { if (open) inputEl?.focus(); });

  const groups = $derived.by(() => {
    const s = q.trim().toLowerCase();
    const list = !s
      ? OPENDBC_CARS
      : OPENDBC_CARS.filter(c => (c.brand + ' ' + c.model + ' ' + c.years).toLowerCase().includes(s));
    const out: Record<string, Car[]> = {};
    for (const c of list) (out[c.brand] ||= []).push(c);
    for (const k of Object.keys(out)) {
      out[k].sort((a, b) => (b.popular ? 1 : 0) - (a.popular ? 1 : 0));
    }
    return out;
  });
</script>

{#if open}
  <div
    class="cp-backdrop"
    onclick={onClose}
    onkeydown={(e) => e.key === 'Escape' && onClose()}
    role="presentation"
  >
    <div
      class="frame car-picker"
      onclick={(e) => e.stopPropagation()}
      onkeydown={(e) => e.stopPropagation()}
      role="dialog"
      aria-modal="true"
      aria-label="Pick your vehicle"
      tabindex="-1"
    >
      <div class="frame__head">
        <span class="row-flex">
          <span style="font-size: 14px">🚗</span>
          <span>Pick your vehicle</span>
        </span>
        <span class="row-flex">
          {#if current}
            <button class="btn btn--sm btn--ghost" onclick={onClear} title="Clear vehicle profile">
              Clear
            </button>
          {/if}
          <button class="btn btn--sm btn--ghost btn--icon" onclick={onClose} aria-label="Close">
            <Icon name="x" size={13} />
          </button>
        </span>
      </div>
      <div style="padding: 8px 10px; border-bottom: 1px solid var(--dc-border)">
        <input
          bind:this={inputEl}
          class="inp"
          placeholder="Search brand or model — e.g. Model 3, RAV4, Ioniq…"
          bind:value={q}
          style="width: 100%"
        />
      </div>
      <div class="car-picker__list">
        {#if Object.keys(groups).length === 0}
          <div class="muted" style="padding: 16px; text-align: center; font-size: 12px">
            No matches.
            <a href="https://github.com/commaai/opendbc" target="_blank" rel="noopener" style="color: var(--dc-accent)">
              Browse opendbc
            </a>
            for the full list.
          </div>
        {/if}
        {#each Object.entries(groups) as [brand, cars]}
          <div class="car-group">
            <div class="car-group__head mono">
              {brand}
              {#if brand !== 'Tesla'}
                <span class="car-group__soon">coming soon</span>
              {/if}
            </div>
            {#each cars as c}
              <button
                class={'car-row' + (c.id === current?.id ? ' car-row--active' : '') + (c.brand !== 'Tesla' ? ' car-row--disabled' : '')}
                onclick={() => c.brand === 'Tesla' && onPick(c)}
                disabled={c.brand !== 'Tesla'}
                title={c.brand !== 'Tesla' ? 'Only Tesla is supported for now' : undefined}
              >
                <span class="car-row__model">{c.model}</span>
                <span class="car-row__years mono">{c.years}</span>
                {#if c.popular}
                  <span class="car-row__pop">popular</span>
                {:else}
                  <span></span>
                {/if}
                {#if c.id === current?.id}
                  <Icon name="check" size={13} />
                {:else}
                  <span></span>
                {/if}
              </button>
            {/each}
          </div>
        {/each}
      </div>
      <div class="car-picker__foot mono">
        List from
        <a href="https://github.com/commaai/opendbc" target="_blank" rel="noopener">commaai/opendbc</a>
        · {OPENDBC_CARS.length} platforms shown · syncs nightly
      </div>
    </div>
  </div>
{/if}
