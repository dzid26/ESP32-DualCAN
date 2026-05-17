<script lang="ts">
  import { toast } from '../lib/toast.svelte';
</script>

<div class="toast-region" aria-live="polite" aria-atomic="false">
  {#each toast.list as t (t.id)}
    <div
      class="toast toast--{t.severity}"
      role="status"
      onclick={() => toast.dismiss(t.id)}
      onkeydown={(e) => { if (e.key === 'Enter' || e.key === ' ') toast.dismiss(t.id); }}
      tabindex="0"
    >
      <span class="toast__msg">{t.message}</span>
      {#if t.link}
        <a
          href={t.link.href}
          target="_blank"
          rel="noopener"
          class="toast__link"
          onclick={(e) => e.stopPropagation()}
        >
          {t.link.text} ↗
        </a>
      {/if}
      <span class="toast__close" aria-hidden="true">×</span>
    </div>
  {/each}
</div>

<style>
  .toast-region {
    position: fixed;
    top: 12px;
    right: 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    z-index: 9999;
    pointer-events: none;
    max-width: min(360px, calc(100vw - 24px));
  }
  /* On narrow screens (mobile), anchor to bottom — closer to thumb. */
  @media (max-width: 600px) {
    .toast-region {
      top: auto;
      bottom: 12px;
      left: 12px;
      right: 12px;
      max-width: none;
    }
  }
  .toast {
    pointer-events: auto;
    background: var(--dc-surface-2, #1a1a2e);
    border: 1px solid var(--dc-border, #333);
    border-radius: 6px;
    padding: 10px 12px;
    display: flex;
    align-items: flex-start;
    gap: 8px;
    font-size: 13px;
    line-height: 1.4;
    cursor: pointer;
    text-align: left;
    color: var(--dc-text);
    box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
    animation: toast-in 160ms ease-out;
  }
  .toast:focus-visible {
    outline: 2px solid var(--dc-accent);
    outline-offset: 2px;
  }
  .toast--info {
    border-left: 3px solid var(--dc-accent, #5b8dff);
  }
  .toast--warn {
    border-left: 3px solid var(--dc-warn, #e6a23c);
  }
  .toast--error {
    border-left: 3px solid var(--dc-err-text, #e25c5c);
  }
  .toast__msg {
    flex: 1;
    min-width: 0;
  }
  .toast__link {
    color: var(--dc-accent);
    text-decoration: none;
    font-size: 11px;
    white-space: nowrap;
    flex-shrink: 0;
    margin-top: 2px;
  }
  .toast__link:hover {
    text-decoration: underline;
  }
  .toast__close {
    color: var(--dc-text-fade);
    font-size: 16px;
    line-height: 1;
    margin-top: -1px;
    flex-shrink: 0;
  }
  @keyframes toast-in {
    from { opacity: 0; transform: translateY(-6px); }
    to   { opacity: 1; transform: translateY(0); }
  }
</style>
