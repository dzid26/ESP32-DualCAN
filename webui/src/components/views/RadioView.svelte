<script lang="ts">
  import SectionHead from '../SectionHead.svelte';
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 10px; height: 100%;">
  <SectionHead title="Radio" />
  <div style="flex: 1; min-height: 0; border-radius: 6px; overflow: hidden; border: 1px solid var(--dc-border, #333);">
    <iframe
      src="https://null.perchance.org/radio-box-2-lite"
      style="width: 100%; height: 100%; border: none; background: #000;"
      title="Perchance Radio"
      allow="autoplay"
      sandbox="allow-scripts allow-same-origin allow-popups"
    ></iframe>
  </div>
</div>
