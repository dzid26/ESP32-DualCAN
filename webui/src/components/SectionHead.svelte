<script lang="ts">
  import type { Snippet } from 'svelte';

  let { title, sub, actions }: {
    title: string;
    sub?: string;
    actions?: Snippet;
  } = $props();
</script>

<div class="head">
  <div style="flex: 1; min-width: 240px">
    <h2 style="margin: 0; font-size: 18px; font-weight: 700; color: var(--dc-text)">{title}</h2>
    {#if sub}
      <p style="margin: 2px 0 0; color: var(--dc-text-fade); font-size: 12px">{sub}</p>
    {/if}
  </div>
  {#if actions}
    <div class="row-flex" style="flex-wrap: wrap">{@render actions()}</div>
  {/if}
</div>

<style>
  .head {
    display: flex; align-items: flex-start; gap: 12px;
    margin-bottom: 10px; flex-wrap: wrap;
  }
</style>
