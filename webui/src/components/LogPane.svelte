<script lang="ts">
  import { app } from '../lib/store.svelte';
  import type { LogLevel } from '../transport/protocol';
  import Icon from './Icon.svelte';

  const LEVELS: LogLevel[] = ['none', 'error', 'warn', 'info', 'debug', 'verbose'];

  let logContainer: HTMLDivElement | null = null;
  let isAtBottom = $state(true);
  let dragging = $state(false);
  let dragStartY = 0;
  let dragStartH = 0;

  /** Pattern: filename.extension:line: (e.g. "my_script.be:42:") */
  const LINK_RE = /([\w.-]+\.(?:be|bep)):(\d+):/g;

  function linkify(msg: string): string {
    return msg.replace(LINK_RE,
      '<span class="log-link" data-fn="$1" data-line="$2">$1:$2:</span>');
  }

  function handleLogClick(e: MouseEvent) {
    const btn = (e.target as HTMLElement).closest('.log-link') as HTMLElement | null;
    if (!btn) return;
    const fn = (btn.dataset.fn ?? '').trim();
    const line = parseInt(btn.dataset.line ?? '', 10);
    if (!fn || isNaN(line)) return;
    app.gotoEditorLine = { filename: fn, line };
    app.setView('scripts');
  }

  function handleLogKeydown(e: KeyboardEvent) {
    if (e.key !== 'Enter') return;
    const btn = (e.currentTarget as HTMLElement).querySelector('.log-link') as HTMLElement | null;
    if (!btn) return;
    const fn = (btn.dataset.fn ?? '').trim();
    const line = parseInt(btn.dataset.line ?? '', 10);
    if (!fn || isNaN(line)) return;
    app.gotoEditorLine = { filename: fn, line };
    app.setView('scripts');
  }

  function levelColor(level: string): string {
    switch (level) {
      case 'warn':  return 'var(--dc-warn)';
      case 'error': return 'var(--dc-err-text)';
      case 'debug': return 'var(--dc-text-ghost)';
      default:      return 'var(--dc-info-text)';
    }
  }

  function scrollToBottom() {
    if (logContainer) {
      logContainer.scrollTop = logContainer.scrollHeight;
      isAtBottom = true;
    }
  }

  function handleScroll() {
    if (!logContainer) return;
    // Check if scrolled to bottom (with small tolerance)
    isAtBottom = logContainer.scrollHeight - logContainer.scrollTop - logContainer.clientHeight < 10;
  }

  function onDragStart(e: PointerEvent) {
    const t = e.target as HTMLElement;
    if (t.closest('button, select, label, input')) return;
    dragging = true;
    dragStartY = e.clientY;
    dragStartH = app.logHeight;
    t.setPointerCapture(e.pointerId);
    e.preventDefault();
  }

  function onDragMove(e: PointerEvent) {
    if (!dragging) return;
    const delta = dragStartY - e.clientY;
    const h = Math.min(800, Math.max(80, dragStartH + delta));
    app.setLogHeight(h);
  }

  function onDragEnd() {
    dragging = false;
  }

  $effect(() => {
    app.logs; // Depend on logs array
    if (isAtBottom) {
      // Use setTimeout to ensure DOM has updated
      setTimeout(scrollToBottom, 0);
    }
  });
</script>

<div class="logpane" style:height={app.logHeight + 'px'}>
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="logpane__head"
    role="separator"
    aria-orientation="vertical"
    onpointerdown={onDragStart}
    onpointermove={onDragMove}
    onpointerup={onDragEnd}
    class:logpane__head--dragging={dragging}
  >
    <span class="row-flex" style="color: var(--dc-text-dim); font-size: 12px">
      <Icon name="log" size={13} />
      <span>Logs</span>
      <span class="mono" style="padding: 0 6px; background: var(--dc-surface); border: 1px solid var(--dc-border); border-radius: 3px; font-size: 10px">
        {app.logs.length}
      </span>
    </span>
    <span class="row-flex">
      <select
        class="logpane__level"
        value={app.logLevel}
        onchange={(e) => app.setLogLevel((e.currentTarget as HTMLSelectElement).value as LogLevel)}
        title="Runtime log level (firmware-side filter)"
      >
        {#each LEVELS as lvl}
          <option value={lvl}>{lvl}</option>
        {/each}
      </select>
      <button class="btn btn--sm btn--ghost" onclick={() => app.clearLogs()}>Clear</button>
      <button
        class="btn btn--sm btn--ghost"
        onclick={() => scrollToBottom()}
        title="Scroll to bottom"
      >
        <span style="display: flex; flex-direction: column; gap: -2px; line-height: 0.7">
          <Icon name="chevD" size={22} />
        </span>
      </button>
      <button class="btn btn--sm btn--ghost" onclick={() => (app.logOpen = false)} aria-label="Close logs">
        <Icon name="x" size={12} />
      </button>
    </span>
  </div>
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="logpane__body mono" bind:this={logContainer} onscroll={handleScroll} onclick={handleLogClick} onkeydown={handleLogKeydown} role="button" tabindex="0">
    {#each app.logs as l}
      <div class="logpane__row">
        <span style="color: var(--dc-text-ghost)">{l.ts}</span>
        <span style={`text-transform: uppercase; font-size: 10px; color: ${levelColor(l.level)}`}>{l.level}</span>
        <span style="color: var(--dc-text-fade)">{l.src}</span>
        <!-- svelte-ignore a11y_click_events_have_key_events -->
        <span style="color: var(--dc-text-dim); white-space: pre-wrap">{@html linkify(l.msg)}</span>
      </div>
    {/each}
  </div>
</div>

<style>
  .logpane {
    border-top: 1px solid var(--dc-border);
    display: flex; flex-direction: column;
    background: var(--dc-surface-deep);
    position: relative;
  }
  .logpane__head {
    display: flex; justify-content: space-between; align-items: center;
    padding: 0px 10px; border-bottom: 1px solid var(--dc-border);
    cursor: ns-resize;
    user-select: none;
  }
  .logpane__head--dragging {
    background: rgba(224, 123, 31, 0.08);
  }
  .logpane__head button, .logpane__head select {
    cursor: pointer;
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
  .logpane__level {
    background: var(--dc-surface);
    color: var(--dc-text-dim);
    border: 1px solid var(--dc-border);
    border-radius: 3px;
    font-size: 11px;
    padding: 1px 4px;
  }
  :global(.log-link) {
    background: none;
    border: none;
    color: var(--dc-accent);
    cursor: pointer;
    padding: 0;
    margin: 0;
    font: inherit;
    display: inline;
    text-decoration: underline;
    text-decoration-style: dotted;
  }
  :global(.log-link:hover) {
    color: var(--dc-accent-hi);
    text-decoration-style: solid;
  }
</style>
