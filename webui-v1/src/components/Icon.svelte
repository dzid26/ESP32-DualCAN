<script lang="ts">
  import { ICON_PATHS, type IconName } from '../lib/icons';

  let { name, size = 18, title }: { name: IconName; size?: number; title?: string } = $props();
  const paths = $derived(ICON_PATHS[name]);
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill="none"
  stroke="currentColor"
  stroke-width="1.75"
  stroke-linecap="round"
  stroke-linejoin="round"
  role="img"
  aria-label={title ?? name}
>
  {#if title}<title>{title}</title>{/if}
  {#each paths as d}
    <path {d} />
  {/each}
</svg>
