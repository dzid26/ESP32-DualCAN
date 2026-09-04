<script lang="ts">
  import { FILLED_ICONS, type FilledIconName } from '../lib/icons';

  let { name, size = 18, title }: { name: FilledIconName; size?: number; title?: string } = $props();
  const icon = FILLED_ICONS[name];
</script>

<svg
  xmlns="http://www.w3.org/2000/svg"
  width={size}
  height={size}
  viewBox={icon.viewBox}
  fill="currentColor"
  role="img"
  aria-label={title ?? name}
>
  {#if title}<title>{title}</title>{/if}
  <path d={icon.path} />
</svg>
