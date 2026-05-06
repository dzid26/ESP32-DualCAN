<script lang="ts">
  import { app, type ViewId } from '../lib/store.svelte';
  import { NAV_ITEMS } from '../lib/nav';

  let { open, onClose }: { open: boolean; onClose: () => void } = $props();

  type Cmd = { id: string; label: string; group: string; run: () => void };

  let q = $state('');
  let idx = $state(0);
  let inputEl: HTMLInputElement | undefined = $state();

  $effect(() => { if (open) { inputEl?.focus(); idx = 0; q = ''; } });

  const commands = $derived<Cmd[]>([
    ...NAV_ITEMS.map(n => ({
      id: 'goto:' + n.id,
      label: 'Go to ' + n.label,
      group: n.group,
      run: () => { app.setView(n.id as ViewId); onClose(); },
    })),
    { id: 'conn',  label: app.connected ? 'Disconnect' : 'Connect',
      group: 'Device', run: () => { app.toggleConnect(); onClose(); } },
    { id: 'sim',   label: app.simulation ? 'Turn off simulation mode' : 'Enable simulation mode',
      group: 'Device', run: () => { app.toggleSim(); onClose(); } },
    { id: 'kill',  label: app.killed ? 'Release kill switch' : 'Engage kill switch',
      group: 'Device', run: () => { app.toggleKill(); onClose(); } },
    { id: 'logs',  label: 'Toggle log console', group: 'View',
      run: () => { app.logOpen = !app.logOpen; onClose(); } },
    { id: 'ota',   label: 'Check for firmware update', group: 'Device',
      run: () => { app.setView('settings'); onClose(); } },
    { id: 'upload', label: 'Upload DBC file…', group: 'Signals',
      run: () => { app.setView('dbc'); onClose(); } },
    { id: 'new',   label: 'New script…', group: 'Automations',
      run: () => { app.setView('scripts'); onClose(); } },
    { id: 'trace', label: 'Start frame trace', group: 'Reverse eng.',
      run: () => { app.setView('trace'); onClose(); } },
    { id: 'snap',  label: 'Snapshot baseline (diff)', group: 'Reverse eng.',
      run: () => { app.setView('capture'); onClose(); } },
  ]);

  const filtered = $derived.by(() => {
    if (!q.trim()) return commands;
    const n = q.toLowerCase();
    return commands.filter(c => c.label.toLowerCase().includes(n) || c.group.toLowerCase().includes(n));
  });

  function onKey(e: KeyboardEvent): void {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      idx = Math.min(idx + 1, filtered.length - 1);
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      idx = Math.max(idx - 1, 0);
    } else if (e.key === 'Enter') {
      e.preventDefault();
      filtered[idx]?.run();
    }
  }
</script>

{#if open}
  <div
    class="cp-backdrop"
    onclick={onClose}
    onkeydown={(e) => e.key === 'Escape' && onClose()}
    role="presentation"
  >
    <div class="cp" onclick={(e) => e.stopPropagation()} role="dialog" aria-modal="true" aria-label="Command palette">
      <input
        bind:this={inputEl}
        class="cp__input"
        bind:value={q}
        placeholder="Type a command or jump to a view…"
        oninput={() => (idx = 0)}
        onkeydown={onKey}
      />
      <div class="cp__list">
        {#each filtered as c, i}
          <div
            class={'cp__item' + (i === idx ? ' cp__item--active' : '')}
            onmouseenter={() => (idx = i)}
            onclick={c.run}
            role="button"
            tabindex="-1"
          >
            <span>{c.label}</span>
            <span class="cp__item-group">{c.group}</span>
          </div>
        {/each}
        {#if filtered.length === 0}
          <div class="cp__item ghost">No matches.</div>
        {/if}
      </div>
    </div>
  </div>
{/if}
