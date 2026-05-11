<script lang="ts">
  let { version = 'v0.3.1', latest = true, channel = 'stable', progress = null, onclick = null }:
    { version?: string; latest?: boolean | string; channel?: string; progress?: number | null; onclick?: (() => void) | null } = $props();

  const isMismatch = $derived(latest === 'mismatch');
  const isLatest = $derived(latest === true);
  const statusClass = $derived(isMismatch ? 'pip--ver-err' : isLatest ? 'pip--ver-latest' : 'pip--ver-stale');
  const ledClass = $derived(isMismatch ? 'pip__led--err' : isLatest ? 'pip__led--ok' : 'pip__led--warn');
  const statusLabel = $derived(isMismatch ? 'mismatch' : isLatest ? 'latest' : 'update');
  const title = $derived(isMismatch
    ? `Firmware ${version} · protocol mismatch — reconnect device`
    : isLatest
    ? `Firmware ${version} · ${channel} · latest release`
    : `Firmware ${version} · stable update available — open Settings → Firmware`);
</script>

<button
  class={'pip pip--ver ' + statusClass + (onclick ? ' pip--clickable' : '')}
  {title}
  {onclick}
>
  {#if progress !== null}
    <div class="ota-mini-bg" style="margin-right: 4px; width: 50px">
      <div class="ota-mini-fill" style="width: {progress}%"></div>
    </div>
  {/if}
  <span class="mono" style="color: var(--dc-text-dim)">{version}</span>
  <span class={'pip__led ' + ledClass} aria-hidden="true"></span>
  <span class="ghost mono" style="font-size: 10px">{statusLabel}</span>
</button>
