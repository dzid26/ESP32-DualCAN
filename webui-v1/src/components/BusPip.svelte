<script lang="ts">
  let { id, name, active, rate }:
    { id: number; name: string; active: boolean; rate: string } = $props();
</script>

<div class="pip" title={`${name} · bus ${id}`}>
  <span class="mono" style="color: var(--dc-text-dim)">bus{id}</span>
  <span class={'pip__dot ' + (active ? 'pip__dot--ok' : '')}></span>
  <span class="ghost mono" style="font-size: 10px">{active ? rate : 'idle'}</span>
</div>
