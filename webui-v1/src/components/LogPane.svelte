<script lang="ts">
  import { app } from '../lib/store.svelte';
  import Icon from './Icon.svelte';

  function levelColor(level: string): string {
    switch (level) {
      case 'warn':  return 'var(--dc-warn)';
      case 'error': return 'var(--dc-err-text)';
      case 'debug': return 'var(--dc-text-ghost)';
      default:      return 'var(--dc-info-text)';
    }
  }
</script>

<div class="logpane">
  <div class="logpane__head">
    <span class="row-flex" style="color: var(--dc-text-dim); font-size: 12px">
      <Icon name="log" size={13} />
      <span>Logs</span>
      <span class="mono" style="padding: 0 6px; background: var(--dc-surface); border: 1px solid var(--dc-border); border-radius: 3px; font-size: 10px">
        {app.logs.length}
      </span>
    </span>
    <span class="row-flex">
      <button class="btn btn--sm btn--ghost" onclick={() => app.clearLogs()}>Clear</button>
      <button class="btn btn--sm btn--ghost" onclick={() => (app.logOpen = false)} aria-label="Close logs">
        <Icon name="x" size={12} />
      </button>
    </span>
  </div>
  <div class="logpane__body mono">
    {#each app.logs as l}
      <div class="logpane__row">
        <span style="color: var(--dc-text-ghost)">{l.ts}</span>
        <span style={`text-transform: uppercase; font-size: 10px; color: ${levelColor(l.level)}`}>{l.level}</span>
        <span style="color: var(--dc-text-fade)">{l.src}</span>
        <span style="color: var(--dc-text-dim); white-space: pre-wrap">{l.msg}</span>
      </div>
    {/each}
  </div>
</div>

<style>
  .logpane {
    height: 180px;
    border-top: 1px solid var(--dc-border);
    display: flex; flex-direction: column;
    background: var(--dc-surface-deep);
  }
  .logpane__head {
    display: flex; justify-content: space-between; align-items: center;
    padding: 4px 10px; border-bottom: 1px solid var(--dc-border);
  }
  .logpane__body {
    flex: 1; overflow-y: auto;
    padding: 4px 8px; font-size: 11px; line-height: 1.55;
  }
  .logpane__row {
    display: grid;
    grid-template-columns: 96px 52px 90px 1fr;
    gap: 8px; white-space: nowrap;
    border-bottom: 1px solid var(--dc-border);
  }
</style>
