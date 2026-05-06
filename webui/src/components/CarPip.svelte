<script lang="ts">
  import type { Car } from '../lib/cars';
  import Icon from './Icon.svelte';

  let { car, onOpen }: { car: Car | null; onOpen: () => void } = $props();
</script>

<button
  class={'pip pip--clickable pip--car' + (car ? ' pip--car-set' : ' pip--car-unset')}
  onclick={onOpen}
  title={car ? `Vehicle profile: ${car.brand} ${car.model} ${car.years}` : 'Pick a vehicle profile (opendbc)'}
>
  <span style="font-size: 13px">{car?.icon ?? '🚗'}</span>
  {#if car}
    <span style="color: var(--dc-text-dim)">{car.brand}</span>
    <span class="ghost mono" style="font-size: 10px">{car.model}</span>
  {:else}
    <span style="color: var(--dc-warn)">Pick a car</span>
    <span class="pip__led pip__led--warn" aria-hidden="true"></span>
  {/if}
  <Icon name="chevD" size={11} />
</button>
