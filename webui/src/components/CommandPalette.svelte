<script lang="ts">
  import { Dialog, Command } from 'bits-ui';
  import { app, type ViewId } from '../lib/store.svelte';
  import { NAV_ITEMS } from '../lib/nav';

  let { open, onClose }: { open: boolean; onClose: () => void } = $props();

  type Cmd = { id: string; label: string; group: string; run: () => void };

  const commands = $derived<Cmd[]>([
    ...NAV_ITEMS.map(n => ({
      id: 'goto:' + n.id,
      label: 'Go to ' + n.label,
      group: n.group,
      run: () => { app.setView(n.id as ViewId); onClose(); },
    })),
    { id: 'conn',   label: app.connected ? 'Disconnect' : 'Connect',
      group: 'Device', run: () => { app.toggleConnect(); onClose(); } },
    { id: 'sim',    label: app.simulation ? 'Turn off simulation mode' : 'Enable simulation mode',
      group: 'Device', run: () => { app.toggleSim(); onClose(); } },
    { id: 'kill',   label: app.killed ? 'Release kill switch' : 'Engage kill switch',
      group: 'Device', run: () => { app.toggleKill(); onClose(); } },
    { id: 'logs',   label: 'Toggle log console', group: 'View',
      run: () => { app.logOpen = !app.logOpen; onClose(); } },
    { id: 'ota',    label: 'Check for firmware update', group: 'Device',
      run: () => { app.setView('settings'); onClose(); } },
    { id: 'upload', label: 'Upload DBC file…', group: 'Signals',
      run: () => { app.setView('dbc'); onClose(); } },
    { id: 'new',    label: 'New script…', group: 'Automations',
      run: () => { app.setView('scripts'); onClose(); } },
    { id: 'trace',  label: 'Start frame trace', group: 'Reverse eng.',
      run: () => { app.setView('trace'); onClose(); } },
    { id: 'snap',   label: 'Snapshot baseline (diff)', group: 'Reverse eng.',
      run: () => { app.setView('capture'); onClose(); } },
  ]);
</script>

<!--
  Dialog: focus trapping, scroll lock, Escape-to-close, aria-modal, focus restoration.
  Command: fuzzy filter on item value+keywords, ↑↓ Enter keyboard nav, aria-listbox.
  autofocus on Input ensures focus lands in the search field after Dialog's focus sweep.
-->
<Dialog.Root {open} onOpenChange={(v) => !v && onClose()}>
  <Dialog.Portal>
    <Dialog.Overlay class="cp-overlay" />
    <Dialog.Content class="cp" aria-label="Command palette">
      <Command.Root class="cp-root" label="Command palette">
        <Command.Input class="cp__input" placeholder="Type a command or jump to a view…" autofocus />
        <Command.List class="cp__list">
          {#each commands as c}
            <Command.Item value={c.label} keywords={[c.group]} onSelect={c.run} class="cp__item">
              <span>{c.label}</span>
              <span class="cp__item-group">{c.group}</span>
            </Command.Item>
          {/each}
          <Command.Empty class="cp__item ghost">No matches.</Command.Empty>
        </Command.List>
      </Command.Root>
    </Dialog.Content>
  </Dialog.Portal>
</Dialog.Root>
