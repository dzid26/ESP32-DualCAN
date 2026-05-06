<script lang="ts">
  let { version = 'v0.3.1', latest = true, channel = 'stable' }:
    { version?: string; latest?: boolean; channel?: string } = $props();

  const title = $derived(latest
    ? `Firmware ${version} · ${channel} · latest release on GitHub`
    : `Firmware ${version} · update available — open Settings → Firmware`);
</script>

<div
  class={'pip pip--ver ' + (latest ? 'pip--ver-latest' : 'pip--ver-stale')}
  {title}
>
  <span class="mono" style="color: var(--dc-text-dim)">{version}</span>
  <span class={'pip__led ' + (latest ? 'pip__led--ok' : 'pip__led--warn')} aria-hidden="true"></span>
  <span class="ghost mono" style="font-size: 10px">{latest ? 'latest' : 'update'}</span>
</div>
