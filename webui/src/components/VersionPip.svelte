<script lang="ts">
  let { version = 'v0.3.1', latest = true, channel = 'stable', progress = null, onclick = null }:
    { version?: string; latest?: boolean; channel?: string; progress?: number | null; onclick?: (() => void) | null } = $props();

  const title = $derived(latest
    ? `Firmware ${version} · ${channel} · latest release on GitHub`
    : `Firmware ${version} · update available — open Settings → Firmware`);
</script>

<button
  class={'pip pip--ver ' + (latest ? 'pip--ver-latest' : 'pip--ver-stale') + (onclick ? ' pip--clickable' : '')}
  {title}
  {onclick}
>
  {#if progress !== null}
    <div class="ota-mini-bg" style="margin-right: 4px; width: 50px">
      <div class="ota-mini-fill" style="width: {progress}%"></div>
    </div>
  {/if}
  <span class="mono" style="color: var(--dc-text-dim)">{version}</span>
  <span class={'pip__led ' + (latest ? 'pip__led--ok' : 'pip__led--warn')} aria-hidden="true"></span>
  <span class="ghost mono" style="font-size: 10px">{latest ? 'latest' : 'update'}</span>
</button>
