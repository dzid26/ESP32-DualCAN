<script lang="ts">
  import { marked } from 'marked';
  import { highlightBerryToHtml } from '../editor/highlight-berry';

  let { onClose }: { onClose: () => void } = $props();

  let content = $state('');
  let loading = $state(true);
  let error = $state<string | null>(null);
  let bodyEl: HTMLDivElement | undefined;

  /* ── Custom marked renderer with Berry highlighting ── */
  const renderer = new marked.Renderer();
  renderer.code = ({ text, lang }) => {
    const langClass = lang ? ' language-' + encodeURIComponent(lang) : '';
    if (lang === 'berry' || lang === 'lua' || !lang) {
      return '<pre><code class="hljs' + langClass + '">' + highlightBerryToHtml(text) + '</code></pre>\n';
    }
    return '<pre><code class="' + langClass + '">' + highlightBerryToHtml(text) + '</code></pre>\n';
  };
  renderer.link = ({ href, text }) => {
    // marked already sanitizes href; text may contain inline HTML (e.g. <code>)
    return '<a href="' + href + '" target="_blank" rel="noopener noreferrer">' + text + '</a>';
  };

  marked.setOptions({ renderer });

  $effect(() => {
    fetch('/docs/scripting.md')
      .then(r => {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.text();
      })
      .then(async text => {
        content = await marked.parse(text);
        loading = false;
      })
      .catch(e => { error = e.message; loading = false; });
  });

  /* One-shot scroll restoration after content loads */
  let _loaded = $state(false);
  $effect(() => {
    if (!loading && content && !_loaded) {
      _loaded = true;
      if (_lastScroll > 0) {
        requestAnimationFrame(() => {
          if (bodyEl) bodyEl.scrollTop = _lastScroll;
        });
      }
    }
  });

  /* scroll is already saved on every onscroll event — nothing to do on destroy */
</script>

<script module>
  /** Remembers scroll position across open/close cycles */
  let _lastScroll = 0;
</script>

<div style="position: fixed; inset: 0; background: rgba(10, 8, 4, 0.55); z-index: 200; display: flex; align-items: center; justify-content: center; padding: 20px;" onclick={onClose} role="presentation">
  <div style="display: flex; flex-direction: column; height: 85vh; width: min(780px, 95vw); background: var(--dc-surface); border: 1px solid var(--dc-border); border-radius: 8px; overflow: hidden; box-shadow: 0 8px 32px rgba(0,0,0,0.35);" onkeydown={(e) => e.key === 'Escape' && onClose()} onclick={(e) => e.stopPropagation()} role="dialog" aria-label="Scripting guide" tabindex="-1">
    <div style="display: flex; align-items: center; justify-content: space-between; padding: 10px 14px; border-bottom: 1px solid var(--dc-border); flex-shrink: 0;">
      <span class="mono" style="font-size: 13px; font-weight: 600">Scripting guide</span>
      <button class="btn btn--sm btn--ghost btn--icon" onclick={onClose} aria-label="Close">
        <svg width="14" height="14" viewBox="0 0 14 14" fill="none">
          <path d="M2 2L12 12M12 2L2 12" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
        </svg>
      </button>
    </div>
    <div bind:this={bodyEl} style="flex: 1; min-height: 0; overflow-y: auto; padding: 16px 20px;"
      onscroll={() => { if (bodyEl) _lastScroll = bodyEl.scrollTop; }}>
      {#if loading}
        <div class="empty" style="margin: 20px">Loading…</div>
      {:else if error}
        <div class="empty" style="margin: 20px; border-color: var(--dc-err-border); color: var(--dc-err-text)">
          Failed to load: {error}
        </div>
      {:else}
        <div class="guide-content" style="max-width: 720px;">{@html content}</div>
      {/if}
    </div>
  </div>
</div>

<style>
  .guide-content {
    line-height: 1.6;
  }
  .guide-content :global(h1) {
    font-size: 1.4em;
    font-weight: 700;
    margin: 0 0 12px 0;
    padding-bottom: 6px;
    border-bottom: 1px solid var(--dc-border);
  }
  .guide-content :global(h2) {
    font-size: 1.15em;
    font-weight: 600;
    margin: 20px 0 8px 0;
  }
  .guide-content :global(h3) {
    font-size: 1em;
    font-weight: 600;
    margin: 16px 0 6px 0;
    color: var(--dc-text-fade);
  }
  .guide-content :global(p) { margin: 0 0 10px 0; }
  .guide-content :global(a) { color: var(--dc-accent-hi); }
  .guide-content :global(code) {
    font-family: var(--dc-font-mono);
    font-size: 0.88em;
    background: var(--dc-surface-2);
    padding: 1px 5px;
    border-radius: 3px;
  }
  .guide-content :global(pre) {
    background: var(--dc-surface-deep);
    border: 1px solid var(--dc-border);
    border-radius: 5px;
    padding: 10px 14px;
    overflow-x: auto;
    margin: 10px 0;
  }
  .guide-content :global(pre code) {
    background: none;
    padding: 0;
    font-size: 0.85em;
    line-height: 1.45;
    white-space: pre;
  }
  .guide-content :global(ul), .guide-content :global(ol) { margin: 6px 0; padding-left: 22px; }
  .guide-content :global(li) { margin: 3px 0; }
  .guide-content :global(blockquote) {
    border-left: 3px solid var(--dc-accent);
    margin: 10px 0;
    padding: 4px 12px;
    background: var(--dc-surface);
    color: var(--dc-text-fade);
  }
  .guide-content :global(hr) {
    border: none;
    border-top: 1px solid var(--dc-border);
    margin: 16px 0;
  }
  .guide-content :global(table) {
    border-collapse: collapse;
    width: 100%;
    margin: 10px 0;
    font-size: 0.9em;
  }
  .guide-content :global(th), .guide-content :global(td) {
    border: 1px solid var(--dc-border);
    padding: 6px 10px;
    text-align: left;
    white-space: nowrap;
  }
  .guide-content :global(td:last-child) {
    white-space: normal;
  }
  .guide-content :global(th) {
    background: var(--dc-surface-2);
    font-weight: 600;
  }
  .guide-content :global(strong) { font-weight: 600; }
</style>
