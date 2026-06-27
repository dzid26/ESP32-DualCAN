<script lang="ts">
  let { load }: { load: number } = $props();
</script>

<div class="pip" title="Total FreeRTOS CPU load">
  <span class="mono" style="color: var(--dc-text-dim)">CPU</span>
  <span class="mono" style="font-size: 11px; min-width: 3.5ch; display: inline-block; text-align: right">{load}%</span>
</div>
